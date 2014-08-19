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

unsigned int job_id = 0;

#define LOCK(lock) do {						\
		int __ret = pthread_mutex_lock(lock);		\
		assert(__ret == 0 && "failed mx lock");		\
	} while (0)

#define UNLOCK(lock) do {					\
		int __ret = pthread_mutex_unlock(lock);		\
		assert(__ret == 0 && "failed mx unlock");	\
	} while (0)

#define WAIT(cond, mx) do {					\
		int __ret = pthread_cond_wait((cond), (mx));	\
		assert(__ret == 0 && "failed wait");		\
	} while (0)

#define SIGNAL(cond) do {					\
		int __ret = pthread_cond_signal(cond);		\
		assert(__ret == 0 && "failed signal");		\
	} while (0)

#define BROADCAST(cond) do {					\
		int __ret = pthread_cond_broadcast(cond);	\
		assert(__ret == 0 && "failed broadcast");	\
	} while (0)

struct job *new_job(struct pp_set *config)
{
	struct job *j = XMALLOC(1, struct job);
	j->config = config;
	bool result = create_config_file(&j->config_file);
	assert(result && "could not create config file for job");
	result = create_results_file(&j->config_file);
	assert(result && "could not create results file for job");

	j->id = __sync_fetch_and_add(&job_id, 1);

	/* compute generation */
	// TODO

	j->done = false;
	int ret = pthread_cond_init(&j->done_cvar, NULL);
	assert(ret == 0 && "could not initialize job cvar");
	ret = pthread_mutex_init(&j->lock, NULL);
	assert(ret == 0 && "could not initialize job lock");

	return j;
}

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;
	LOCK(&j->lock);
	// TODO: read config and write config file
	UNLOCK(&j->lock);

	// TODO: spawn process
	// TODO: wait on result socket
	// TODO: add reported DRs to PP registry

	LOCK(&j->lock);
	// TODO: write some results?
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
