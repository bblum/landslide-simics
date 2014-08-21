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
#include "pp.h"
#include "sync.h"

static unsigned int job_id = 0;

static pthread_mutex_t compile_landslide_lock = PTHREAD_MUTEX_INITIALIZER;

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

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;

	/* 2-way communication with landslide. pipefd[0] is the read end. */
	int input_pipefds[2];
	int output_pipefds[2];
	XPIPE(input_pipefds);
	XPIPE(output_pipefds);

	LOCK(&j->config_lock);

	/* write config file */
	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		XWRITE(&j->config_file, "%s\n", pp->config_str);
	}

	/* our output is the child's input and V. V. */
	XWRITE(&j->config_file, "output_pipe %d\n", input_pipefds[1]);
	XWRITE(&j->config_file, "input_pipe %d\n", output_pipefds[0]);

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

		/* assemble commandline arguments */
		char *execname = "./" LANDSLIDE_PROGNAME;
		char *const argv[4] = {
			[0] = execname,
			[1] = j->config_file.filename,
			[2] = j->results_file.filename,
			[3] = NULL,
		};
		char *const envp[1] = { [0] = NULL, };

		printf("[JOB %d] '%s %s %s > %s 2> %s'\n", j->id, execname,
		       j->config_file.filename, j->results_file.filename,
		       j->log_stdout.filename, j->log_stderr.filename);

		/* fixup file descriptors */
		unset_cloexec(input_pipefds[1]);
		unset_cloexec(output_pipefds[0]);

		XDUP2(j->log_stdout.fd, STDOUT_FILENO);
		XDUP2(j->log_stderr.fd, STDERR_FILENO);
		unset_cloexec(STDOUT_FILENO);
		unset_cloexec(STDERR_FILENO);

		XCHDIR(LANDSLIDE_PATH);

		execve(execname, argv, envp);

		EXPECT(false, "execve() failed\n");
		exit(EXIT_FAILURE);
	}

	/* parent */
	// FIXME: rename these pipefds
	XCLOSE(input_pipefds[1]);
	XCLOSE(output_pipefds[0]);
	int input_fd  = input_pipefds[0];
	//int output_fd = output_pipefds[0];

	int child_status;
	do {
		char msg_buf[256];
		int ret = read(input_fd, msg_buf, BUF_SIZE);
		printf("ret %d; err %s", ret, strerror(errno));
		// TODO: wait on result socket
		// while ...
		// TODO: add reported DRs to PP registry

		/* clean up after child */
		pid_t result_pid = waitpid(landslide_pid, &child_status, 0);
		assert(result_pid == landslide_pid && "wait failed");
	} while (WIFSTOPPED(child_status) || WIFCONTINUED(child_status));
	printf("Landslide pid %d exited with status %d\n", landslide_pid,
	       WEXITSTATUS(child_status));

	// TODO: remove config files

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
