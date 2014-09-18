/**
 * @file job.h
 * @brief job management
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_JOB_H
#define __ID_JOB_H

#include <pthread.h>

#include "messaging.h"
#include "io.h"

struct pp_set;

struct job {
	/* local state */
	struct pp_set *config; /* shared but read-only after init */
	unsigned int id;
	unsigned int generation; /* max among generations of pps + 1 */
	bool should_reproduce;

	/* shared state */
	bool done;
	pthread_cond_t done_cvar;
	pthread_mutex_t done_lock;
};

void set_job_options(char *test_name, bool verbose, bool leave_logs);
struct job *new_job(struct pp_set *config, bool should_reproduce);
void start_job(struct job *j);
void wait_on_job(struct job *j);
void cancel_job(struct job *j);
void finish_job(struct job *j);

#endif
