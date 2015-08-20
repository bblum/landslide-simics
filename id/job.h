/**
 * @file job.h
 * @brief job management
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_JOB_H
#define __ID_JOB_H

#include <pthread.h>

#include "io.h"
#include "messaging.h"
#include "time.h"

struct pp_set;

struct job {
	/* local state */
	struct pp_set *config; /* shared but read-only after init */
	unsigned int id;
	unsigned int generation; /* max among generations of pps + 1 */
	bool should_reproduce;
	struct file config_file;
	struct file log_stdout;
	struct file log_stderr;

	/* stats -- writable by owner, readable by display thread.
	 * LOCK NOTICE: this is taken while workqueue lock is held. */
	pthread_rwlock_t stats_lock;
	unsigned int elapsed_branches;
	long double estimate_proportion;
	struct human_friendly_time estimate_elapsed;
	struct human_friendly_time estimate_eta;
	long double estimate_eta_numeric;
	bool cancelled;
	bool complete; /* same as 'done' just under different lock */
	bool timed_out;
	char *log_filename;
	char *trace_filename;

	/* misc shared state */
	enum { JOB_NORMAL, JOB_BLOCKED, JOB_DONE } status;
	pthread_cond_t done_cvar; /* workqueue thread waits on this */
	pthread_cond_t blocking_cvar; /* job thread waits on this */
	pthread_mutex_t lifecycle_lock;
};

void set_job_options(char *test_name, bool verbose, bool leave_logs, bool pintos);
bool testing_pintos();

struct job *new_job(struct pp_set *config, bool should_reproduce);
void start_job(struct job *j);
bool wait_on_job(struct job *j); /* true if job blocked, false if done */
void resume_job(struct job *j);

void job_block(struct job *j); /* to be called by job itself */
void print_job_stats(struct job *j, bool pending, bool blocked);
int compare_job_eta(struct job *j0, struct job *j1);

#endif
