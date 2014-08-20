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

#include "common.h"
#include "job.h"
#include "io.h"
#include "pp.h"
#include "sync.h"

static unsigned int job_id = 0;

#define CONFIG_FILE_TEMPLATE "config-id.landslide.XXXXXX"
#define RESULTS_FILE_TEMPLATE "results-id.landslide.XXXXXX"
#define LOG_FILE_TEMPLATE "landslide-id.log.XXXXXX"

struct job *new_job(struct pp_set *config)
{
	struct job *j = XMALLOC(1, struct job);
	j->config = config;

	bool result = create_file(&j->config_file, CONFIG_FILE_TEMPLATE);
	assert(result && "could not create config file for job");
	result = create_file(&j->results_file, RESULTS_FILE_TEMPLATE);
	assert(result && "could not create results file for job");
	result = create_file(&j->log_file, LOG_FILE_TEMPLATE);
	assert(result && "could not create log file for job");

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
	int estimate_pipefd[2];
	int continue_pipefd[2];
	XPIPE(estimate_pipefd);
	XPIPE(continue_pipefd);

	LOCK(&j->config_lock);

	/* write config file */
	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		XWRITE(&j->config_file, "%s\n", pp->config_str);
	}

	XWRITE(&j->config_file, "estimate_pipe %d\n", estimate_pipefd[1]);
	XWRITE(&j->config_file, "continue_pipe %d\n", continue_pipefd[0]);

	UNLOCK(&j->config_lock);

	pid_t landslide_pid = fork();
	if (landslide_pid == 0) {
		/* child process; landslide-to-be */

		unset_cloexec(estimate_pipefd[1]);
		unset_cloexec(continue_pipefd[0]);

		// TODO: dup2 log files to stderr and stdout;
		// make sure cloexec doesn't close them

		/* relocate to pebsim */
		move_file_to(&j->config_file, LANDSLIDE_PATH);
		move_file_to(&j->results_file, LANDSLIDE_PATH);
		XCHDIR(LANDSLIDE_PATH);

		char *execname = "./" LANDSLIDE_PROGNAME;
		char *const argv[4] = {
			[0] = execname,
			[1] = j->config_file.filename,
			[2] = j->results_file.filename,
			[3] = NULL,
		};
		char *const envp[1] = { [0] = NULL, };

		printf("Executing command: '%s %s %s'\n", execname,
		       j->config_file.filename, j->results_file.filename);

		execve(execname, argv, envp);

		EXPECT(false, "execve() failed\n");
		exit(EXIT_FAILURE);
	}

	/* parent */
	XCLOSE(estimate_pipefd[1]);
	XCLOSE(continue_pipefd[0]);

	// TODO: wait on result socket
	// TODO: add reported DRs to PP registry

	// TODO: handle sigchld

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
	FREE(j);
}
