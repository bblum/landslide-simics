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

static unsigned int job_id = 0;

static pthread_mutex_t compile_landslide_lock = PTHREAD_MUTEX_INITIALIZER;

extern char **environ;

// TODO-FIXME: Insert timestamps so log files are sorted chronologically.
#define CONFIG_FILE_TEMPLATE "config-id.landslide.XXXXXX"
#define RESULTS_FILE_TEMPLATE "results-id.landslide.XXXXXX"
#define LOG_FILE_TEMPLATE(x) "landslide-id-" x ".log.XXXXXX"

struct job *new_job(struct pp_set *config)
{
	struct job *j = XMALLOC(1, struct job);
	j->config = config;

	// TODO: decide if results file is needed

	create_file(&j->config_file, CONFIG_FILE_TEMPLATE);
	create_file(&j->results_file, RESULTS_FILE_TEMPLATE);
	create_file(&j->log_stdout, LOG_FILE_TEMPLATE("stdout"));
	create_file(&j->log_stderr, LOG_FILE_TEMPLATE("stderr"));

	j->id = __sync_fetch_and_add(&job_id, 1);
	j->generation = compute_generation(config);
	j->done = false;

	COND_INIT(&j->done_cvar);
	MUTEX_INIT(&j->done_lock);
	MUTEX_INIT(&j->config_lock);

	return j;
}

static NORETURN void spawn_landslide(struct job *j)
{
	/* assemble commandline arguments */
	char *execname = "./" LANDSLIDE_PROGNAME;
	char *const argv[4] = {
		[0] = execname,
		[1] = j->config_file.filename,
		[2] = j->results_file.filename,
		[3] = NULL,
	};

	printf("[JOB %d] '%s %s %s > %s 2> %s'\n", j->id, execname,
	       j->config_file.filename, j->results_file.filename,
	       j->log_stdout.filename, j->log_stderr.filename);

	/* unsetting cloexec not necessary for these */
	XDUP2(j->log_stdout.fd, STDOUT_FILENO);
	XDUP2(j->log_stderr.fd, STDERR_FILENO);

	XCHDIR(LANDSLIDE_PATH);

	execve(execname, argv, environ);

	EXPECT(false, "execve() failed\n");
	exit(EXIT_FAILURE);
}

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;

	/* 2-way communication with landslide. open()ing them will block. */
	char *input_pipe_name = create_fifo("id-input-pipe", j->id);
	char *output_pipe_name = create_fifo("id-output-pipe", j->id);

	LOCK(&j->config_lock);

	/* write config file */
	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		XWRITE(&j->config_file, "%s\n", pp->config_str);
	}

	/* our output is the child's input and V. V. */
	XWRITE(&j->config_file, "output_pipe %s\n", input_pipe_name);
	XWRITE(&j->config_file, "input_pipe %s\n", output_pipe_name);
	XWRITE(&j->config_file, "id_magic %u\n", MESSAGING_MAGIC);

	// XXX: Need to do this here so the parent can have the path into pebsim
	// to properly delete the file, but it brittle-ly causes the child's
	// exec args to have "../pebsim/"s in them that only "happen to work".
	move_file_to(&j->config_file, LANDSLIDE_PATH);
	move_file_to(&j->results_file, LANDSLIDE_PATH);

	UNLOCK(&j->config_lock);

	/* while multiple landslides can run at once, compiling each one from a
	 * different config is mutually exclusive. we'll release this as soon as
	 * we get a message from the child that it's up and running. */
	LOCK(&compile_landslide_lock);

	pid_t landslide_pid = fork();
	if (landslide_pid == 0) {
		/* child process; landslide-to-be */
		spawn_landslide(j);
	}

	/* parent */
	struct file input_pipe;
	struct file output_pipe;

	// TODO: use ansi escape codes for 'invisible' messages
	// XXX: using fifos instead of anonymous pipes, impossible to tell
	// whether e.g. the build failed. maybe use a timed wait?
	open_fifo(&input_pipe, input_pipe_name, O_RDONLY);
	/* should take ~6 seconds for child to come alive */
	bool child_alive = wait_for_child(input_pipe.fd);
	open_fifo(&output_pipe, output_pipe_name, O_WRONLY);

	UNLOCK(&compile_landslide_lock);

	if (child_alive) {
		/* may take as long as the state space is large */
		talk_to_child(input_pipe.fd, output_pipe.fd);
	}

	/* clean up after child */
	int child_status;
	pid_t result_pid = waitpid(landslide_pid, &child_status, 0);
	assert(result_pid == landslide_pid && "wait failed");
	assert(WIFEXITED(child_status) && "wait returned before child exit");
	printf("Landslide pid %d exited with status %d\n", landslide_pid,
	       WEXITSTATUS(child_status));

	// TODO: remove config files
	delete_file(&input_pipe, true);
	delete_file(&output_pipe, true);

	LOCK(&j->done_lock);
	// TODO: interpret results
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

void wait_on_job(struct job *j)
{
	LOCK(&j->done_lock);
	while (!j->done) {
		WAIT(&j->done_cvar, &j->done_lock);
	}
	UNLOCK(&j->done_lock);
}

void finish_job(struct job *j)
{
	wait_on_job(j);
	free_pp_set(j->config);
	delete_file(&j->config_file, true);
	delete_file(&j->results_file, true);
	delete_file(&j->log_stdout, false);
	delete_file(&j->log_stderr, false);
	FREE(j);
}
