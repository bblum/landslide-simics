/**
 * @file work.c
 * @brief workqueue thread pool
 * @author Ben Blum
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <pthread.h>
#include <sys/time.h>

#include "array_list.h"
#include "bug.h"
#include "job.h"
#include "pp.h"
#include "sync.h"
#include "time.h"
#include "work.h"

/* lock order note: PP registry lock taken inside of workqueue_lock */

static bool inited = false;
static bool started = false;
static bool work_done = false;
static bool progress_done = false;
static unsigned int nonblocked_threads;
static ARRAY_LIST(struct job *) workqueue; /* unordered set */
static ARRAY_LIST(struct job *) running_or_done_jobs; /* unordered set */
static ARRAY_LIST(struct job *) blocked_jobs; /* unordered set */
static pthread_mutex_t workqueue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workqueue_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t work_done_cond = PTHREAD_COND_INITIALIZER;

static void check_init()
{
	if (!inited) {
		LOCK(&workqueue_lock);
		if (!inited) {
			ARRAY_LIST_INIT(&workqueue, 16);
			ARRAY_LIST_INIT(&running_or_done_jobs, 16);
			ARRAY_LIST_INIT(&blocked_jobs, 16);
			inited = true;
		}
		UNLOCK(&workqueue_lock);
	}
}

void add_work(struct job *j)
{
	check_init();
	LOCK(&workqueue_lock);
	ARRAY_LIST_APPEND(&workqueue, j);
	UNLOCK(&workqueue_lock);
}

void signal_work()
{
	BROADCAST(&workqueue_cond);
}

bool should_work_block(struct job *j)
{
	bool result;
	LOCK(&workqueue_lock);
	if (ARRAY_LIST_SIZE(&workqueue) != 0) {
		/* A fresh job exists. Switch to it. */
		result = true;
	} else {
		unsigned int num_blocked_jobs = ARRAY_LIST_SIZE(&blocked_jobs);
		if (num_blocked_jobs != 0) {
			/* A different blocked job exists. Switch to it,
			 * but only if its ETA is better. */
			struct job *j2 = *ARRAY_LIST_GET(&blocked_jobs,
							 num_blocked_jobs-1);
			result = compare_job_eta(j, j2) > 0;
		} else {
			/* No other possible jobs to switch to. */
			result = false;
		}
	}
	UNLOCK(&workqueue_lock);
	return result;
}

/* returns NULL if no work is available */
static struct job *get_work(bool *was_blocked)
{
	struct job *best_job = NULL;
	unsigned int best_index;
	unsigned int best_priority;
	unsigned int best_size;

	struct job **j;
	unsigned int i;

	/* If time is up, there may still yet be work to do -- kicking awake
	 * all the blocked jobs so that they can exit cleanly (which they will
	 * do immediately -- see messaging.c). Otherwise, during normal time,
	 * prioritize "fresh" jobs from the pending queue. */
	if (!TIME_UP()) {
		ARRAY_LIST_FOREACH(&workqueue, i, j) {
			unsigned int priority = unexplored_priority((*j)->config);
			unsigned int size = (*j)->config->size;
			if (best_job == NULL || priority < best_priority ||
			    (priority == best_priority && size < best_size)) {
				best_job = *j;
				best_index = i;
				best_priority = priority;
				best_size = size;
			}
		}
	}

	if (best_job != NULL) {
		/* Found best fresh job. Move it to the active queue. */
		ARRAY_LIST_REMOVE_SWAP(&workqueue, best_index);
		ARRAY_LIST_APPEND(&running_or_done_jobs, best_job);
		*was_blocked = false;
		/* Notionally asserting this. Of course it could race and trip.
		 * assert(!TIME_UP()); */
	} else if (ARRAY_LIST_SIZE(&blocked_jobs) > 0) {
		/* No fresh jobs. Take the blocked job with the best ETA. */
		best_index = ARRAY_LIST_SIZE(&blocked_jobs) - 1;
		best_job = *ARRAY_LIST_GET(&blocked_jobs, best_index);
		ARRAY_LIST_REMOVE(&blocked_jobs, best_index);
		ARRAY_LIST_APPEND(&running_or_done_jobs, best_job);
		*was_blocked = true;
	}
	return best_job;
}

static void move_job_to_blocked_queue(struct job *j)
{
	struct job **j2;
	unsigned int i;
	LOCK(&workqueue_lock);

	/* Find job on active jobs list and remove it. */
	ARRAY_LIST_FOREACH(&running_or_done_jobs, i, j2) {
		if (*j2 == j) {
			break;
		}
	}
	assert(i < ARRAY_LIST_SIZE(&running_or_done_jobs) &&
	       "couldn't find now-blocked job on running queue");
	assert(*ARRAY_LIST_GET(&running_or_done_jobs, i) == j);
	ARRAY_LIST_REMOVE_SWAP(&running_or_done_jobs, i);

	/* Put it on blocked queue and bubble-sort it. */
	ARRAY_LIST_APPEND(&blocked_jobs, j);
	i = ARRAY_LIST_SIZE(&blocked_jobs) - 1;
	/* Lower ETA jobs stay closer to the end of the array. */
	while (i > 0 && compare_job_eta(*ARRAY_LIST_GET(&blocked_jobs, i),
					*ARRAY_LIST_GET(&blocked_jobs, i-1)) > 0) {
		DBG("[JOB %d] bubble-sorting blocked job\n", j->id);
		ARRAY_LIST_SWAP(&blocked_jobs, i, i-1);
		i--;
	}

	signal_work();
	UNLOCK(&workqueue_lock);
}

static void process_work(struct job *j, bool was_blocked)
{
	if (bug_already_found(j->config)) {
		/* Optimization for subset-foundabug jobs where the bug was not
		 * found until after the work was added, but before we start the
		 * job. Don't waste time compiling landslide before checking. */
		j->cancelled = true;
	} else {
		if (was_blocked) {
			// DBG("[JOB %d] process(): waking up blocked job\n", j->id);
			resume_job(j);
		} else {
			// DBG("[JOB %d] process(): starting a fresh job\n", j->id);
			start_job(j);
		}

		if (wait_on_job(j)) {
			/* Job became blocked, is still alive. */
			// DBG("[JOB %d] process(): after waiting, job blocked\n", j->id);
			move_job_to_blocked_queue(j);
		} else {
			/* Job ran to completion. */
			// DBG("[JOB %d] process(): after waiting, job complete\n", j->id);
			record_explored_pps(j->config);
		}
	}
}

static void *workqueue_thread(void *arg)
{
	unsigned long id = (unsigned long)arg;
	assert(inited && started);
	DBG("WQ thread %lu ready\n", id);

	LOCK(&workqueue_lock);
	while (true) {
		bool was_blocked;
		struct job *j = get_work(&was_blocked);
		if (j != NULL) {
			UNLOCK(&workqueue_lock);
			DBG("WQ thread %lu got work: job %u\n", id, j->id);
			process_work(j, was_blocked);
			LOCK(&workqueue_lock);
		} else {
			nonblocked_threads--;
			/* wait for new work to be generated */
			if (nonblocked_threads == 0) {
				/* last to finish; all others are done */
				DBG("WQ thread %lu last to finish\n", id);
				BROADCAST(&workqueue_cond);
				break;
			} else {
				/* wait for another thread to make work */
				// DBG("WQ thread %lu waiting for work\n", id);
				WAIT(&workqueue_cond, &workqueue_lock);
				if (nonblocked_threads == 0) {
					/* all other threads ran out of work too */
					DBG("WQ thread %lu woken to quit\n", id);
					break;
				} else {
					/* work appeared; contend for it */
					// DBG("WQ thread %lu woken to try\n", id);
					nonblocked_threads++;
				}
			}

		}
	}
	UNLOCK(&workqueue_lock);
	return NULL;
}


static void print_all_job_stats()
{
	struct human_friendly_time time_since_start;
	const char *header = "==== PROGRESS REPORT ====";

	human_friendly_time(time_elapsed(), &time_since_start);
	PRINT("%s\n", header);
	PRINT("total time elapsed: ");
	print_human_friendly_time(&time_since_start);
	PRINT("\n");

	struct job **j;
	unsigned int i;
	ARRAY_LIST_FOREACH(&running_or_done_jobs, i, j) {
		print_job_stats(*j, false, false);
	}
	ARRAY_LIST_FOREACH(&workqueue, i, j) {
		print_job_stats(*j, true, false);
	}
	ARRAY_LIST_FOREACH(&blocked_jobs, i, j) {
		print_job_stats(*j, false, true);
	}
	for (unsigned int i = 0; i < strlen(header); i++) {
		PRINT("=");
	}
	PRINT("\n");
}

static void *progress_report_thread(void *arg)
{
	unsigned long interval = (unsigned long)arg;

	if (interval == 0) {
		/* edge case - run the cvar protocol with the main thread,
		 * but do nothing in between. */
		LOCK(&workqueue_lock);
		while (!work_done) {
			WAIT(&work_done_cond, &workqueue_lock);
		}
		progress_done = true;
		SIGNAL(&workqueue_cond);
		UNLOCK(&workqueue_lock);
		return NULL;
	}

	LOCK(&workqueue_lock);
	while (true) {
		if (work_done) {
			/* Execution is done. Stop printing progress reports. */
			print_all_job_stats();
			progress_done = true;
			SIGNAL(&workqueue_cond);
			UNLOCK(&workqueue_lock);
			DBG("progress report thr exiting\n");
			break;
		} else {
			/* Wait for the designated interval, or all tests to
			 * finish, whichever comes first. */
			struct timespec wait_time;
			struct timeval current_time;
			XGETTIMEOFDAY(&current_time);
			TIMEVAL_TO_TIMESPEC(&current_time, &wait_time);
			wait_time.tv_sec += interval;
			int ret = pthread_cond_timedwait(&work_done_cond,
							 &workqueue_lock,
							 &wait_time);
			if (ret == ETIMEDOUT) {
				print_all_job_stats();
			} else {
				/* Signalled; execution is done. Go around the
				 * loop again; next time we'll fall out. */
				DBG("progress report thr signalled to exit\n");
				assert(ret == 0 && "timedwait failed?");
				assert(work_done);
			}
		}
	}
	return NULL;
}

void start_work(unsigned long num_cpus, unsigned long progress_report_interval)
{
	check_init();
	assert(!started);
	started = true;

	pthread_t child;
	int ret = pthread_create(&child, NULL, progress_report_thread,
				 (void *)progress_report_interval);
	assert(ret == 0 && "failed create progress report thread");
	ret = pthread_detach(child);
	assert(ret == 0 && "failed detach progress report thread");

	nonblocked_threads = num_cpus;
	for (unsigned long i = 0; i < num_cpus; i++) {
		ret = pthread_create(&child, NULL, workqueue_thread, (void *)i);
		assert(ret == 0 && "failed create worker thread");
		ret = pthread_detach(child);
		assert(ret == 0 && "failed detach worker thread");
	}
}

void wait_to_finish_work()
{
	assert(inited && started);
	LOCK(&workqueue_lock);
	while (nonblocked_threads != 0) {
		WAIT(&workqueue_cond, &workqueue_lock);
	}
	work_done = true;
	SIGNAL(&work_done_cond);
	while (!progress_done) {
		WAIT(&workqueue_cond, &workqueue_lock);
	}
	UNLOCK(&workqueue_lock);
}
