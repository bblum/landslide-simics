/**
 * @file job.c
 * @brief job management
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <pthread.h>

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
	MUTEX_INIT(&j->lock);

	return j;
}

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;

	// TODO: allocate socket

	LOCK(&j->lock);

	/* write config file */
	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		WRITE(&j->config_file, "%s\n", pp->config_str);
	}

	// TODO: write socket no to config

	UNLOCK(&j->lock);

	pid_t landslide_pid = fork();
	if (landslide_pid == 0) {
		/* child */
		// TODO: dup2 log files to stderr and stdout;
		// make sure cloexec doesn't close
	}

	/* parent */
	// TODO: wait on result socket
	// TODO: add reported DRs to PP registry

	LOCK(&j->lock);
	// TODO: interpret results
	j->done = true;
	BROADCAST(&j->done_cvar);
	UNLOCK(&j->lock);

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
	LOCK(&j->lock);
	while (!j->done) {
		WAIT(&j->done_cvar, &j->lock);
	}
	UNLOCK(&j->lock);
}

void finish_job(struct job *j)
{
	wait_on_job(j);
	FREE(j);
}
