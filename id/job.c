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

#include "bug.h"
#include "common.h"
#include "job.h"
#include "io.h"
#include "messaging.h"
#include "pp.h"
#include "sync.h"
#include "time.h"
#include "xcalls.h"

static unsigned int job_id = 0;

static pthread_mutex_t compile_landslide_lock = PTHREAD_MUTEX_INITIALIZER;

extern char **environ;

// TODO-FIXME: Insert timestamps so log files are sorted chronologically.
#define CONFIG_FILE_TEMPLATE "config-id.landslide.XXXXXX"
#define LOG_FILE_TEMPLATE(x) "ls-" x ".log.XXXXXX"

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
	j->status = JOB_NORMAL;
	j->should_reproduce = should_reproduce;

	RWLOCK_INIT(&j->stats_lock);
	j->elapsed_branches = 0;
	j->estimate_proportion = 0;
	human_friendly_time(0.0L, &j->estimate_elapsed);
	human_friendly_time(0.0L, &j->estimate_eta);
	j->estimate_eta_numeric = 0.0L;
	j->cancelled = false;
	j->complete = false;
	j->timed_out = false;
	j->log_filename = NULL;
	j->trace_filename = NULL;

	COND_INIT(&j->done_cvar);
	MUTEX_INIT(&j->lifecycle_lock);

	return j;
}

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;
	struct messaging_state mess;

	create_file(&j->config_file, CONFIG_FILE_TEMPLATE);
	create_file(&j->log_stdout, LOG_FILE_TEMPLATE("setup"));
	create_file(&j->log_stderr, LOG_FILE_TEMPLATE("output"));

	/* write config file */

	// XXX(#120): TEST_CASE must be defined before PPs are specified.
	XWRITE(&j->config_file, "TEST_CASE=%s\n", test_name);
	XWRITE(&j->config_file, "VERBOSE=%d\n", verbose ? 1 : 0);
	// FIXME: Make this more principled instead of a gross hack
	XWRITE(&j->config_file, "without_user_function mutex_lock\n");
	XWRITE(&j->config_file, "without_user_function mutex_unlock\n");

	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		XWRITE(&j->config_file, "%s\n", pp->config_str);
	}
	assert(test_name != NULL);
	// FIXME: Make this principled, as above
	XWRITE(&j->config_file, "without_user_function malloc\n");
	XWRITE(&j->config_file, "without_user_function realloc\n");
	XWRITE(&j->config_file, "without_user_function calloc\n");
	XWRITE(&j->config_file, "without_user_function free\n");

	messaging_init(&mess, &j->config_file, j->id);

	// XXX: Need to do this here so the parent can have the path into pebsim
	// to properly delete the file, but it brittle-ly causes the child's
	// exec args to have "../pebsim/"s in them that only "happen to work".
	move_file_to(&j->config_file, LANDSLIDE_PATH);

	/* while multiple landslides can run at once, compiling each one from a
	 * different config is mutually exclusive. we'll release this as soon as
	 * we get a message from the child that it's up and running. */
	LOCK(&compile_landslide_lock);

	if (bug_already_found(j->config)) {
		DBG("[JOB %d] bug already found; aborting compilation.\n", j->id);
		UNLOCK(&compile_landslide_lock);
		messaging_abort(&mess);
		delete_file(&j->config_file, true);
		delete_file(&j->log_stdout, true);
		delete_file(&j->log_stderr, true);
		WRITE_LOCK(&j->stats_lock);
		j->complete = true;
		j->cancelled = true;
		RW_UNLOCK(&j->stats_lock);
		LOCK(&j->lifecycle_lock);
		j->status = JOB_DONE;
		BROADCAST(&j->done_cvar);
		UNLOCK(&j->lifecycle_lock);
		return NULL;
	}

	WRITE_LOCK(&j->stats_lock);
	j->log_filename = XSTRDUP(j->log_stderr.filename);
	RW_UNLOCK(&j->stats_lock);

	pid_t landslide_pid = fork();
	if (landslide_pid == 0) {
		/* child process; landslide-to-be */
		/* assemble commandline arguments */
		char *execname = "./" LANDSLIDE_PROGNAME;
		char *const argv[4] = {
			[0] = execname,
			[1] = j->config_file.filename,
			[2] = NULL,
		};

		DBG("[JOB %d] '%s %s > %s 2> %s'\n", j->id, execname,
		       j->config_file.filename, j->log_stdout.filename,
		       j->log_stderr.filename);

		/* unsetting cloexec not necessary for these */
		XDUP2(j->log_stdout.fd, STDOUT_FILENO);
		XDUP2(j->log_stderr.fd, STDERR_FILENO);

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
		    j->log_stdout.filename, j->log_stderr.filename);
	}

	int child_status;
	pid_t result_pid = waitpid(landslide_pid, &child_status, 0);
	assert(result_pid == landslide_pid && "wait failed");
	assert(WIFEXITED(child_status) && "wait returned before child exit");
	DBG("Landslide pid %d exited with status %d\n", landslide_pid,
	    WEXITSTATUS(child_status));

	finish_messaging(&mess);

	delete_file(&j->config_file, true);
	bool should_delete = !leave_logs && WEXITSTATUS(child_status) == 0;
	delete_file(&j->log_stdout, should_delete);
	delete_file(&j->log_stderr, should_delete);

	WRITE_LOCK(&j->stats_lock);
	j->complete = true;
	if (should_delete) {
		FREE(j->log_filename);
		j->log_filename = NULL;
	}
	RW_UNLOCK(&j->stats_lock);
	LOCK(&j->lifecycle_lock);
	j->status = JOB_DONE;
	BROADCAST(&j->done_cvar);
	UNLOCK(&j->lifecycle_lock);

	return NULL;
}

/* to be called by job thread of its own volition */
void job_block(struct job *j)
{
	LOCK(&j->lifecycle_lock);
	assert(j->status == JOB_NORMAL);
	j->status = JOB_BLOCKED;
	/* signal workqueue thread to go find something else to do */
	BROADCAST(&j->done_cvar);
	/* wait until there's nothing better to do */
	while (j->status == JOB_BLOCKED) {
		WAIT(&j->blocking_cvar, &j->lifecycle_lock);
	}
	/* we have been woken up and rescheduled */
	assert(j->status == JOB_NORMAL);
	UNLOCK(&j->lifecycle_lock);
}

/* the workqueue threads use the following calls to manage the job threads */

void start_job(struct job *j)
{
	pthread_t child;
	int ret = pthread_create(&child, NULL, run_job, (void *)j);
	assert(ret == 0 && "failed thread fork");
	ret = pthread_detach(child);
	assert(ret == 0 && "failed detach");
}

MUST_CHECK bool wait_on_job(struct job *j)
{
	LOCK(&j->lifecycle_lock);
	while (j->status == JOB_NORMAL) {
		WAIT(&j->done_cvar, &j->lifecycle_lock);
	}
	assert(j->status == JOB_BLOCKED || j->status == JOB_DONE);
	bool rv = j->status == JOB_BLOCKED;
	UNLOCK(&j->lifecycle_lock);
	return rv;
}

/* should be immediately followed by another call to wait_on */
void resume_job(struct job *j)
{
	LOCK(&j->lifecycle_lock);
	assert(j->status == JOB_BLOCKED);
	j->status = JOB_NORMAL;
	SIGNAL(&j->blocking_cvar);
	UNLOCK(&j->lifecycle_lock);
}

void print_job_stats(struct job *j, bool pending, bool blocked)
{
	assert(!pending || !blocked);

	READ_LOCK(&j->stats_lock);
	if (j->cancelled && !verbose) {
		RW_UNLOCK(&j->stats_lock);
		return;
	}
	PRINT("[JOB %d] ", j->id);
	if (j->cancelled) {
		PRINT(COLOUR_DARK COLOUR_RED "CANCELLED\n");
	} else if (j->trace_filename != NULL) {
		PRINT(COLOUR_BOLD COLOUR_RED "BUG FOUND: %s ", j->trace_filename);
		PRINT("(%u interleaving%s tested)\n", j->elapsed_branches,
		      j->elapsed_branches == 1 ? "" : "s");
	} else if (j->timed_out) {
		PRINT(COLOUR_BOLD COLOUR_YELLOW "TIMED OUT ");
		PRINT("(%Lf%%; ETA ", j->estimate_proportion * 100);
		print_human_friendly_time(&j->estimate_eta);
		PRINT(")\n");
	} else if (j->complete) {
		PRINT(COLOUR_BOLD COLOUR_GREEN "COMPLETE ");
		PRINT("(%u interleaving%s tested; ", j->elapsed_branches,
		      j->elapsed_branches == 1 ? "" : "s");
		print_human_friendly_time(&j->estimate_elapsed);
		PRINT(" elapsed)\n");
	} else if (pending) {
		PRINT("Pending...\n");
	} else if (j->elapsed_branches == 0) {
		PRINT("Setting up...\n");
	} else if (blocked) {
		PRINT(COLOUR_DARK COLOUR_MAGENTA "Deferred... ");
		PRINT("(%Lf%%; ETA ", j->estimate_proportion * 100);
		print_human_friendly_time(&j->estimate_eta);
		PRINT(")\n");
	} else {
		PRINT(COLOUR_BOLD COLOUR_MAGENTA "Running ");
		PRINT("(%Lf%%; ETA ", j->estimate_proportion * 100);
		print_human_friendly_time(&j->estimate_eta);
		PRINT(")\n");
	}
	PRINT("       ");
	if (j->log_filename != NULL) {
		// FIXME: "id/" -- better solution for where log files should go
		PRINT(COLOUR_DARK COLOUR_GREY "Log: id/%s -- ", j->log_filename);
	}
	PRINT(COLOUR_DARK COLOUR_GREY "PPs: ");
	printf(COLOUR_GREY);
	print_pp_set(j->config, true);
	PRINT(COLOUR_DEFAULT "\n");
	RW_UNLOCK(&j->stats_lock);
}

int compare_job_eta(struct job *j0, struct job *j1)
{
	READ_LOCK(&j0->stats_lock);
	long double eta0 = j0->estimate_eta_numeric;
	RW_UNLOCK(&j0->stats_lock);

	READ_LOCK(&j1->stats_lock);
	long double eta1 = j1->estimate_eta_numeric;
	RW_UNLOCK(&j1->stats_lock);

	return eta0 == eta1 ? 0 : eta0 < eta1 ? -1 : 1;
}
