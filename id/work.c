/**
 * @file work.c
 * @brief workqueue thread pool
 * @author Ben Blum
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <pthread.h>
#include <sys/time.h>
#include <sys/sysinfo.h>

#include "array_list.h"
#include "bug.h"
#include "job.h"
#include "pp.h"
#include "sync.h"
#include "time.h"
#include "work.h"

/* lock order note: PP registry lock taken inside of workqueue_lock */

typedef ARRAY_LIST(struct job *) job_list_t;
static bool inited = false;
static bool started = false;
static bool work_done = false;
static bool progress_done = false;
static unsigned int nonblocked_threads;
static job_list_t workqueue; /* unordered set */
static job_list_t running_or_done_jobs; /* unordered set */
static job_list_t blocked_jobs; /* unordered set */
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
	bool result = false;
	struct job **j_pending;
	struct job **j_blocked;
	unsigned int i_pending;
	unsigned int i_blocked;

	LOCK(&workqueue_lock);

	/* Are there any pending jobs to run instead? Skip jobs that are strict
	 * supersets of our PP set as we know in advance they'll take longer. */
	ARRAY_LIST_FOREACH(&workqueue, i_pending, j_pending) {
		if (!pp_subset(j->config, (*j_pending)->config)) {
			/* Pending job is smaller or different. One last check:
			 * is it just a bigger version of another blocked job?
			 * Then, prefer that blocked job (in 3rd loop below). */
			bool any_blocked_subsets = false;
			ARRAY_LIST_FOREACH(&blocked_jobs, i_blocked, j_blocked) {
				if (pp_subset((*j_blocked)->config,
					      (*j_pending)->config)) {
					any_blocked_subsets = true;
					break;
				}
			}
			/* The pending job is truly new. Ok to switch to it. */
			if (!any_blocked_subsets) {
				result = true;
				break;
			}
		}
	}

	if (!result) {
		/* Is there another blocked job with better ETA? As before, make
		 * sure it's known in advance to be smaller or different. */
		i_blocked = ARRAY_LIST_SIZE(&blocked_jobs);
		while (i_blocked > 0) {
			i_blocked--;
			j_blocked = ARRAY_LIST_GET(&blocked_jobs, i_blocked);
			if (!pp_subset(j->config, (*j_blocked)->config) &&
			    compare_job_eta(j, (*j_blocked)) > 0) {
				/* Blocked job is smaller with better ETA. */
				result = true;
				break;
			}
		}
	}

	UNLOCK(&workqueue_lock);
	return result;
}

static bool work_already_exists_on(struct pp_set *new_set, job_list_t *q)
{
	struct job **j;
	unsigned int i;
	ARRAY_LIST_FOREACH(q, i, j) {
		if (pp_set_equals(new_set, (*j)->config)) {
			return true;
		}
	}
	return false;
}

bool work_already_exists(struct pp_set *new_set)
{
	bool result = false;

	LOCK(&workqueue_lock);
	if (!result) result = work_already_exists_on(new_set, &workqueue);
	if (!result) result = work_already_exists_on(new_set, &running_or_done_jobs);
	if (!result) result = work_already_exists_on(new_set, &blocked_jobs);
	UNLOCK(&workqueue_lock);

	return result;
}

/* returns NULL if no work is available */
static struct job *get_work(unsigned long wq_id, bool *was_blocked)
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
		unsigned int num_skipped = 0;
		ARRAY_LIST_FOREACH(&workqueue, i, j) {
			/* Don't ever start new pending jobs if they're strict
			 * supersets of already deferred ones. */
			struct job **j2;
			unsigned int i2;
			bool any_deferred_subsets = false;
			ARRAY_LIST_FOREACH(&blocked_jobs, i2, j2) {
				if (pp_subset((*j2)->config, (*j)->config)) {
					any_deferred_subsets = true;
					break;
				}
			}
			if (any_deferred_subsets) {
				num_skipped++;
				continue;
			}

			/* Is the pending job the best one so far? */
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
		if (num_skipped > 0) {
			DBG("WQ thread %lu skipped %u pending jobs, each "
			    "bigger than one deferred.\n", wq_id, num_skipped);
		}
	}

	if (best_job != NULL) {
		/* Found best fresh job. Move it to the active queue. */
		ARRAY_LIST_REMOVE_SWAP(&workqueue, best_index);
		ARRAY_LIST_APPEND(&running_or_done_jobs, best_job);
		*was_blocked = false;
		/* Notionally asserting this. Of course it could race and trip.
		 * assert(!TIME_UP()); */
	} else {
		/* No fresh job. Find the blocked job with the best ETA.
		 * However, if a job's ETA looks good but it has a strict subset
		 * job farther up the list with way worse ETA, we'll trust that
		 * bad ETA instead. At the very least, we'll prefer to resume
		 * the subset job instead. But even better still would be a 3rd
		 * unrelated job with better ETA than that subset job. (Compare
		 * this reasoning to the 2nd half of should-work-block().) */
		best_index = ARRAY_LIST_SIZE(&blocked_jobs);
		while (best_index > 0) {
			best_index--;
			best_job = *ARRAY_LIST_GET(&blocked_jobs, best_index);
			/* Check for a subset job with bigger (worse) ETA. */
			bool subset_has_worse_eta = false;
			for (i = 0; i < best_index; i++) {
				j = ARRAY_LIST_GET(&blocked_jobs, i);
				if (pp_subset((*j)->config, best_job->config)) {
					/* This blocked job is unacceptable. */
					subset_has_worse_eta = true;
					break;
				}
			}
			if (!subset_has_worse_eta) {
				/* No matches, above. This job is acceptable. */
				break;
			}
		}
		/* Was a best blocked job found? (The list can be empty ofc.) */
		if (best_job != NULL) {
			ARRAY_LIST_REMOVE(&blocked_jobs, best_index);
			ARRAY_LIST_APPEND(&running_or_done_jobs, best_job);
			*was_blocked = true;
		}
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
			READ_LOCK(&j->stats_lock);
			bool need_rerun = j->need_rerun;
			RW_UNLOCK(&j->stats_lock);
			if (need_rerun) {
				WARN("[JOB %d] failed on branch 1, needs rerun\n",
				     j->id);
				add_work(new_job(j->config, j->should_reproduce));
			} else
			/* Job ran to completion. */
			/* Don't let "small" jobs mark DRs as verified: they're
			 * not likely to explore the interleavings we care about
			 * without the other PPs enabled as well. */
			if (j->should_reproduce) {
				record_explored_pps(j->config);
			}
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
		struct job *j = get_work(id, &was_blocked);
		if (j != NULL) {
			UNLOCK(&workqueue_lock);
			DBG("WQ thread %lu got work: job %u\n", id, j->id);
			start_using_cpu(id);
			j->current_cpu = id;
			process_work(j, was_blocked);
			j->current_cpu = (unsigned long)-1;
			stop_using_cpu(id);
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

static bool get_ram_usage(unsigned long *totalram, unsigned long *availram)
{
	bool have_memavail = false;
	FILE *proc_meminfo = fopen("/proc/meminfo", "r");
	if (proc_meminfo != NULL) {
		char buf[BUF_SIZE];
		while (fgets(buf, BUF_SIZE, proc_meminfo) != NULL) {
			if (sscanf(buf, "MemAvailable: %lu kB", availram) == 1) {
				have_memavail = true;
				*availram *= 1024;
				break;
			}
		}
		fclose(proc_meminfo);
	}

	struct sysinfo info;
	int ret = sysinfo(&info);
	if (ret == 0) {
		*totalram = info.totalram;
		if (!have_memavail) {
			WARN("MemAvailable not supported, "
			     "falling back to sysinfo to check ram usage\n");
			*availram = info.freeram;
		}
		return true;
	}

	return false;
}

#define RAM_USAGE_DANGERZONE 90 /* percent */
#define KILL_DEFERRED_JOBS   50 /* percent */

static void cant_swap() /* called with workqueue lock held */
{
	/* Too many suspended deferred jobs can hog memory. If the machine is in
	 * danger of swapping, kill off half the */
	unsigned long totalram, availram;
	if (!get_ram_usage(&totalram, &availram)) {
		WARN("can't swap, making bad decisions\n");
		return;
	}

	/* i know, i know, check for overflow */
	if (availram > totalram * (100 - RAM_USAGE_DANGERZONE) / 100) {
		return;
	}

	WARN("Killing %d%% of deferred jobs to avoid swapping...\n",
	     KILL_DEFERRED_JOBS);

	unsigned int num_to_kill =
		ARRAY_LIST_SIZE(&blocked_jobs) * KILL_DEFERRED_JOBS / 100;

	for (unsigned int i = 0; i < num_to_kill; i++) {
		/* check for race with all blocked jobs waking */
		if (ARRAY_LIST_SIZE(&blocked_jobs) == 0) {
			break;
		}
		/* jobs with the worst ETAs live at the front of the queue;
		 * we're least likely to ever resume those ngrmadly. */
		struct job *victim = *ARRAY_LIST_GET(&blocked_jobs, 0);
		ARRAY_LIST_REMOVE(&blocked_jobs, 0);
		ARRAY_LIST_APPEND(&running_or_done_jobs, victim);

		UNLOCK(&workqueue_lock);

		/* wake the job but set its kill flag so its next should_abort
		 * message returns true before any more branches execute. */
		WRITE_LOCK(&victim->stats_lock);
		victim->kill_job = true;
		RW_UNLOCK(&victim->stats_lock);
		resume_job(victim);
		if (wait_on_job(victim)) {
			assert(0 && "can't swap, eating stuff you make me chew");
		}

		LOCK(&workqueue_lock);
	}
}

extern bool verbose;
#define TOO_MANY_PENDING_JOBS 5

static void print_all_job_stats()
{
	struct human_friendly_time time_since_start;
	const char *header = "==== PROGRESS REPORT ====";

	human_friendly_time(time_elapsed(), &time_since_start);
	PRINT("%s\n", header);
	PRINT("total time elapsed: ");
	print_human_friendly_time(&time_since_start);
	PRINT("\n");

	bool summarize_pending = !verbose &&
		ARRAY_LIST_SIZE(&workqueue) >= TOO_MANY_PENDING_JOBS;

	struct job **j;
	unsigned int i;
	ARRAY_LIST_FOREACH(&running_or_done_jobs, i, j) {
		print_job_stats(*j, false, false);
	}
	if (!summarize_pending) {
		ARRAY_LIST_FOREACH(&workqueue, i, j) {
			print_job_stats(*j, true, false);
		}
	}
	ARRAY_LIST_FOREACH(&blocked_jobs, i, j) {
		print_job_stats(*j, false, true);
	}
	if (summarize_pending) {
		PRINT("And %d more pending jobs should time allow.\n",
		      ARRAY_LIST_SIZE(&workqueue));
	}
	print_free_re_malloc_false_positives();
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
				cant_swap(); /* x100 */
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
