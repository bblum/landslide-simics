/**
 * @file job.c
 * @brief job management
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common.h"
#include "job.h"
#include "io.h"
#include "messaging.h"
#include "pp.h"
#include "sync.h"
#include "xcalls.h"

static unsigned int job_id = 0;

static pthread_mutex_t compile_landslide_lock = PTHREAD_MUTEX_INITIALIZER;

extern char **environ;

// TODO-FIXME: Insert timestamps so log files are sorted chronologically.
#define CONFIG_FILE_TEMPLATE "config-id.landslide.XXXXXX"
#define LOG_FILE_TEMPLATE(x) "landslide-id-" x ".log.XXXXXX"

char *test_name = NULL;
bool verbose = false;
bool leave_logs = false;

void set_job_options(char *arg_test_name, bool arg_verbose, bool arg_leave_logs)
{
	test_name = XSTRDUP(arg_test_name);
	verbose = arg_verbose;
	leave_logs = arg_leave_logs;
}

struct job *new_job(struct pp_set *config, bool should_reproduce)
{
	struct job *j = XMALLOC(1, struct job);
	j->config = config;

	j->id = __sync_fetch_and_add(&job_id, 1);
	j->generation = compute_generation(config);
	j->done = false;
	j->should_reproduce = should_reproduce;

	COND_INIT(&j->done_cvar);
	MUTEX_INIT(&j->done_lock);

	return j;
}

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;
	struct file config_file;
	struct file log_stdout;
	struct file log_stderr;
	struct messaging_state mess;

	create_file(&config_file, CONFIG_FILE_TEMPLATE);
	create_file(&log_stdout, LOG_FILE_TEMPLATE("stdout"));
	create_file(&log_stderr, LOG_FILE_TEMPLATE("stderr"));

	/* write config file */

	// XXX(#120): TEST_CASE must be defined before PPs are specified.
	XWRITE(&config_file, "TEST_CASE=%s\n", test_name);
	XWRITE(&config_file, "VERBOSE=%d\n", verbose ? 1 : 0);
	// FIXME: Make this more principled instead of a gross hack
	XWRITE(&config_file, "without_user_function mutex_lock\n");
	XWRITE(&config_file, "without_user_function mutex_unlock\n");

	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		XWRITE(&config_file, "%s\n", pp->config_str);
	}
	assert(test_name != NULL);
	// FIXME: Make this principled, as above
	XWRITE(&config_file, "without_user_function malloc\n");
	XWRITE(&config_file, "without_user_function realloc\n");
	XWRITE(&config_file, "without_user_function calloc\n");
	XWRITE(&config_file, "without_user_function free\n");

	messaging_init(&mess, &config_file, j->id);

	// XXX: Need to do this here so the parent can have the path into pebsim
	// to properly delete the file, but it brittle-ly causes the child's
	// exec args to have "../pebsim/"s in them that only "happen to work".
	move_file_to(&config_file, LANDSLIDE_PATH);

	/* while multiple landslides can run at once, compiling each one from a
	 * different config is mutually exclusive. we'll release this as soon as
	 * we get a message from the child that it's up and running. */
	LOCK(&compile_landslide_lock);

	pid_t landslide_pid = fork();
	if (landslide_pid == 0) {
		/* child process; landslide-to-be */
		/* assemble commandline arguments */
		char *execname = "./" LANDSLIDE_PROGNAME;
		char *const argv[4] = {
			[0] = execname,
			[1] = config_file.filename,
			[2] = NULL,
		};

		DBG("[JOB %d] '%s %s > %s 2> %s'\n", j->id, execname,
		       config_file.filename, log_stdout.filename, log_stderr.filename);

		/* unsetting cloexec not necessary for these */
		XDUP2(log_stdout.fd, STDOUT_FILENO);
		XDUP2(log_stderr.fd, STDERR_FILENO);

		XCHDIR(LANDSLIDE_PATH);

		execve(execname, argv, environ);

		EXPECT(false, "execve() failed\n");
		exit(EXIT_FAILURE);
	}

	/* parent */

	/* should take ~6 seconds for child to come alive */
	bool child_alive = wait_for_child(&mess);

	UNLOCK(&compile_landslide_lock);

	if (child_alive) {
		/* may take as long as the state space is large */
		talk_to_child(&mess, j);
	} else {
		// TODO: record job in "failed to run" list or some such
		ERR("[JOB %d] There was a problem setting up Landslide.\n", j->id);
		// TODO: err_pp_set or some such
		ERR("[JOB %d] For details see %s and %s\n", j->id,
		    log_stdout.filename, log_stderr.filename);
	}

	int child_status;
	pid_t result_pid = waitpid(landslide_pid, &child_status, 0);
	assert(result_pid == landslide_pid && "wait failed");
	assert(WIFEXITED(child_status) && "wait returned before child exit");
	DBG("Landslide pid %d exited with status %d\n", landslide_pid,
	    WEXITSTATUS(child_status));

	finish_messaging(&mess);

	delete_file(&config_file, true);
	bool should_delete = !leave_logs && WEXITSTATUS(child_status) == 0;
	delete_file(&log_stdout, should_delete);
	delete_file(&log_stderr, should_delete);

	LOCK(&j->done_lock);
	j->done = true;
	BROADCAST(&j->done_cvar);
	UNLOCK(&j->done_lock);

	return NULL;
}

void start_job(struct job *j)
{
	pthread_t child;
	int ret = pthread_create(&child, NULL, run_job, (void *)j);
	assert(ret == 0 && "failed thread fork");
	ret = pthread_detach(child);
	assert(ret == 0 && "failed detach");
}

// TODO: allow for job to block until later

// TODO: add block_job

void wait_on_job(struct job *j)
{
	LOCK(&j->done_lock);
	while (!j->done) {
		WAIT(&j->done_cvar, &j->done_lock);
	}
	UNLOCK(&j->done_lock);
}

void cancel_job(struct job *j)
{
	free_pp_set(j->config);
	FREE(j);
}

void finish_job(struct job *j)
{
	wait_on_job(j);
	record_explored_pps(j->config);
	cancel_job(j);
}
