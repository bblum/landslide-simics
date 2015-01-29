/**
 * @file work.c
 * @brief workqueue thread pool
 * @author Ben Blum
 */

#define _XOPEN_SOURCE 700

#include <pthread.h>

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
static unsigned int nonblocked_threads;
static ARRAY_LIST(struct job *) workqueue; /* unordered set */
static ARRAY_LIST(struct job *) nonpending_jobs; /* unordered set */
static pthread_mutex_t workqueue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t workqueue_cond = PTHREAD_COND_INITIALIZER;

static void check_init()
{
	if (!inited) {
		LOCK(&workqueue_lock);
		if (!inited) {
			ARRAY_LIST_INIT(&workqueue, 16);
			ARRAY_LIST_INIT(&nonpending_jobs, 16);
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

/* returns NULL if no work is available */
static struct job *get_work()
{
	struct job *best_job = NULL;
	unsigned int best_index;
	unsigned int best_priority;
	unsigned int best_size;

	struct job **j;
	unsigned int i;

	if (TIME_UP()) {
		return NULL;
	}

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
	if (best_job != NULL) {
		ARRAY_LIST_REMOVE_SWAP(&workqueue, best_index);
		ARRAY_LIST_APPEND(&nonpending_jobs, best_job);
	}
	return best_job;
}

static void process_work(struct job *j)
{
	if (bug_already_found(j->config)) {
		/* Optimization for subset-foundabug jobs where the bug was not
		 * found until after the work was added, but before we start the
		 * job. Don't waste time compiling landslide before checking. */
		j->cancelled = true;
	} else {
		// TODO: upgrade to allow for blocked jobs
		// TODO: when running e.g. mx lock+unlock job, periodically
		// check for lower priority jobs.
		start_job(j);
		wait_on_job(j);
		finish_job(j);
	}
}

static void *workqueue_thread(void *arg)
{
	unsigned long id = (unsigned long)arg;
	assert(inited && started);
	DBG("WQ thread %lu ready\n", id);

	LOCK(&workqueue_lock);
	while (true) {
		struct job *j = get_work();
		if (j != NULL) {
			UNLOCK(&workqueue_lock);
			DBG("WQ thread %lu got work: job %u\n", id, j->id);
			process_work(j);
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
				DBG("WQ thread %lu waiting for work\n", id);
				WAIT(&workqueue_cond, &workqueue_lock);
				if (nonblocked_threads == 0) {
					/* all other threads ran out of work too */
					DBG("WQ thread %lu woken to quit\n", id);
					break;
				} else {
					/* work appeared; contend for it */
					DBG("WQ thread %lu woken to try\n", id);
					nonblocked_threads++;
				}
			}

		}
	}
	UNLOCK(&workqueue_lock);
	return NULL;
}

void start_work(unsigned long num_cpus)
{
	check_init();
	assert(!started);
	started = true;
	nonblocked_threads = num_cpus;
	for (unsigned long i = 0; i < num_cpus; i++) {
		pthread_t child;
		int ret = pthread_create(&child, NULL, workqueue_thread, (void *)i);
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
	UNLOCK(&workqueue_lock);
}
