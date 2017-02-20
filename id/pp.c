/**
 * @file pp.c
 * @brief preemption points
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <limits.h>
#include <pthread.h>
#include <string.h>

#include "common.h"
#include "job.h"
#include "pp.h"
#include "sync.h"
#include "xcalls.h"

/* global state */
static pthread_rwlock_t pp_registry_lock = PTHREAD_RWLOCK_INITIALIZER;
static unsigned int next_id; /* also represents 1 + max legal index */
static unsigned int max_generation = 0;
static unsigned int registry_capacity;
static struct pp **registry = NULL; /* NULL means not yet initialized */

#define INITIAL_CAPACITY 16

extern bool verbose;
extern bool pure_hb;

/******************************************************************************
 * PP registry
 ******************************************************************************/

static struct pp *pp_append(char *config_str, char *short_str, char *long_str,
			    unsigned int priority, bool deterministic,
			    bool free_re_malloc, unsigned int generation)
{
	struct pp *pp = XMALLOC(1, struct pp);
	pp->config_str     = config_str;
	pp->short_str      = short_str;
	pp->long_str       = long_str;
	pp->priority       = priority;
	pp->id             = next_id;
	pp->generation     = generation;
	pp->deterministic  = deterministic;
	pp->free_re_malloc = free_re_malloc;
	pp->explored       = false;

	assert(pp->priority != 0);

	if (generation > max_generation) {
		// TODO: have a printing framework
		max_generation = generation;
	}

	// TODO: convert to use array-list macross
	if (next_id == registry_capacity) {
		assert(registry_capacity < UINT_MAX / 2);
		struct pp **resized = XMALLOC(registry_capacity * 2, struct pp *);
		memcpy(resized, registry, next_id * sizeof(struct pp *));
		FREE(registry);
		registry = resized;
		registry_capacity *= 2;
	}
	registry[next_id] = pp;
	next_id++;
	return pp;
}

static void check_init() {
	READ_LOCK(&pp_registry_lock);
	bool need_init = registry == NULL;
	RW_UNLOCK(&pp_registry_lock);
	if (need_init) {
		WRITE_LOCK(&pp_registry_lock);
		if (registry == NULL) { /* DCL */
			registry_capacity = INITIAL_CAPACITY;
			next_id = 0;
			registry = XMALLOC(registry_capacity, struct pp *);
			struct pp *pp = pp_append(
				XSTRDUP(testing_pintos() ?
					"within_function sema_down" :
					testing_pathos() ?
					"within_function mutex_lock" :
					"within_user_function mutex_lock"),
				XSTRDUP(testing_pintos() ?
					"sema_down" : "mutex_lock"),
				XSTRDUP("<at beginning of mutex_lock>"),
				PRIORITY_MUTEX_LOCK, true, false, max_generation);
			assert(pp->id == 0);
			pp = pp_append(
				XSTRDUP(testing_pintos() ?
					"within_function sema_up" :
					testing_pathos() ?
					"within_function mutex_unlock" :
					"within_user_function mutex_unlock"),
				XSTRDUP(testing_pintos() ?
					"sema_up" : "mutex_unlock"),
				XSTRDUP("<at end of mutex_unlock>"),
				PRIORITY_MUTEX_UNLOCK, true, false, max_generation);
			assert(pp->id == 1);
			assert(next_id == 2);
			if (testing_pintos() || testing_pathos()) {
				pp = pp_append(
					XSTRDUP(testing_pintos() ?
						"within_function intr_disable" :
						"within_function preempt_disable"),
					XSTRDUP("cli"),
					XSTRDUP("<just before cli>"),
					PRIORITY_CLI, true, false, max_generation);
				assert(pp->id == 2);
				pp = pp_append(
					XSTRDUP(testing_pintos() ?
						"within_function intr_enable" :
						"within_function preempt_enable"),
					XSTRDUP("sti"),
					XSTRDUP("<just after sti>"),
					PRIORITY_STI, true, false, max_generation);
				assert(pp->id == 3);
				assert(next_id == 4);
			}
		}
		RW_UNLOCK(&pp_registry_lock);
	}
}

struct pp *pp_new(char *config_str, char *short_str, char *long_str,
		  unsigned int priority, bool deterministic, bool free_re_malloc,
		  unsigned int generation, bool *duplicate)
{
	struct pp *result;
	bool already_present = false;
	*duplicate = false;

	check_init();
	WRITE_LOCK(&pp_registry_lock);
	/* try to find existing one */
	for (unsigned int i = 0; i < next_id; i++) {
		result = registry[i];
		if (0 == strcmp(config_str, result->config_str)) {
			already_present = true;
			*duplicate = true;
			if (priority < result->priority) {
				DBG("updating priority of '%s' from %d to %d\n",
				    config_str, result->priority, priority);
				result->priority = priority;
				result->generation = generation;
			}
			if (deterministic && !result->deterministic) {
				DBG("updating '%s' to be a deterministic DR\n",
				    config_str);
				result->deterministic = true;
			}
			if (!free_re_malloc && result->free_re_malloc) {
				DBG("updating '%s' to NOT be a free-re-malloc "
				    "FP DR (it was found for realsies\n",
				    config_str);
				result->free_re_malloc = false;
			}
			break;
		}
	}

	if (!already_present) {
		DBG("adding new pp '%s' priority %d\n", config_str, priority);
		if (IS_DATA_RACE(priority)) {
			WARN("Found a %sracy access at %s\n",
			     pure_hb ? "" : "potentially-", long_str);
		}
		result = pp_append(XSTRDUP(config_str), XSTRDUP(short_str),
				   XSTRDUP(long_str), priority, deterministic,
				   free_re_malloc, generation);
	}
	RW_UNLOCK(&pp_registry_lock);
	return result;
}

struct pp *pp_get(unsigned int id)
{
	check_init();
	READ_LOCK(&pp_registry_lock);
	assert(id < next_id && "nonexistent pp of that id");
	struct pp *result = registry[id];
	RW_UNLOCK(&pp_registry_lock);
	assert(result->id == id && "inconsistent PP id in PP registry");
	return result;
}

static void _print_live_data_race_pps_unlocked()
{
	bool any_exist = false;
	for (unsigned int i = 0; i < next_id; i++) {
		struct pp *pp = registry[i];
		if (IS_DATA_RACE(pp->priority) && !pp->explored) {
			// XXX: Better way of figuring out how to suppress
			// unreadable obfuscated kernel addresses.
			const char *gross_special_case = "0x00102917";
			if (0 == strncmp(pp->long_str, gross_special_case,
					 strlen(gross_special_case))) {
				continue;
			}
			if (!any_exist) {
				/* first one such found; print header */
				any_exist = true;
				if (pure_hb) {
					WARN("NOTE: I found the following DATA "
					     "RACES (see slide #26 in my lecture).\n");
					WARN("I didn't have enough time to check "
					     "whether they could lead to bugs,\n");
					WARN("but they are still suspicious, so "
					     "please inspect them yourself:\n");
				} else {
					WARN("NOTE: I discovered the following "
					     "POTENTIALLY-RACY accesses,\n");
					WARN("but was not able to confirm them "
					     "either way as benign or buggy.\n");
					WARN("You may wish to inspect them manually, "
					     "if the following info is convenient:\n");
				}
			}
			WARN("Data race at %s\n", pp->long_str);
		}
	}
}

void print_live_data_race_pps()
{
	READ_LOCK(&pp_registry_lock);
	_print_live_data_race_pps_unlocked();
	RW_UNLOCK(&pp_registry_lock);
}

/* Signal-handler-safe. */
void try_print_live_data_race_pps()
{
	int ret = pthread_rwlock_tryrdlock(&pp_registry_lock);
	if (ret == 0) {
		_print_live_data_race_pps_unlocked();
		RW_UNLOCK(&pp_registry_lock);
	} else {
		DBG("Couldn't get PP registry lock to print DR PPs.\n");
	}
}

void print_free_re_malloc_false_positives()
{
	if (!verbose) return;

	bool any_exist = false;

	READ_LOCK(&pp_registry_lock);
	for (unsigned int i = 0; i < next_id; i++) {
		struct pp *pp = registry[i];
		if (pp->free_re_malloc) {
			assert(IS_DATA_RACE(pp->priority));
			if (!any_exist) {
				any_exist = true;
				WARN("NOTE: I avoided the following "
				     "free-re-malloc false positives.\n");
			}
			WARN("FP free-re-malloc race %sat %s\n", pp->deterministic ? "" : "[NONDET] ", pp->long_str);
		}
	}
	RW_UNLOCK(&pp_registry_lock);

	if (!any_exist) {
		WARN("No free-re-malloc false positives were avoided.\n");
	}
}

/******************************************************************************
 * PP sets
 ******************************************************************************/

static struct pp_set *alloc_pp_set(unsigned int capacity)
{
	unsigned int struct_size =
		sizeof(struct pp_set) + (capacity * sizeof(bool));
	return (struct pp_set *)XMALLOC(struct_size, char /* c.c */);
}

struct pp_set *create_pp_set(unsigned int pp_mask)
{
	check_init();
	READ_LOCK(&pp_registry_lock);
	struct pp_set *set = alloc_pp_set(next_id);
	set->size = 0;
	set->capacity = next_id;
	for (unsigned int i = 0; i < next_id; i++) {
		set->array[i] = ((pp_mask & pp_get(i)->priority) != 0);
		if (set->array[i]) {
			set->size++;
		}
	}
	RW_UNLOCK(&pp_registry_lock);
	return set;
}

struct pp_set *clone_pp_set(struct pp_set *set)
{
	struct pp_set *new_set = alloc_pp_set(set->capacity);
	new_set->size = set->size;
	new_set->capacity = set->capacity;
	for (unsigned int i = 0; i < new_set->capacity; i++) {
		new_set->array[i] = set->array[i];
	}
	return new_set;
}

struct pp_set *add_pp_to_set(struct pp_set *set, struct pp *pp)
{
	unsigned int new_capacity = MAX(set->capacity, pp->id + 1);
	struct pp_set *new_set = alloc_pp_set(new_capacity);
	new_set->size = set->size;
	new_set->capacity = new_capacity;
	for (unsigned int i = 0; i < new_set->capacity; i++) {
		new_set->array[i] = i < set->capacity && set->array[i];
	}
	if (!new_set->array[pp->id]) {
		new_set->array[pp->id] = true;
		new_set->size++;
	}
	return new_set;
}

void free_pp_set(struct pp_set *set)
{
	FREE(set);
}

void print_pp_set(struct pp_set *set, bool short_strs)
{
	struct pp *pp;
	// FIXME: clean this up
	printf("{ ");
	log_msg(NULL, "{ ");
	FOR_EACH_PP(pp, set) {
		printf("'%s' ", short_strs ? pp->short_str : pp->config_str);
		log_msg(NULL, "'%s' ", short_strs ? pp->short_str : pp->config_str);

		if (verbose && !pp->deterministic) {
			printf("[NONDET] ");
			log_msg(NULL, "[NONDET] ");
		}
		if (verbose && pp->free_re_malloc) {
			printf("[FRM-FP] ");
			log_msg(NULL, "[FRM-FP] ");
		}
	}
	printf("}");
	log_msg(NULL, "}");
}

bool pp_set_contains(struct pp_set *set, struct pp *pp)
{
	return pp->id < set->capacity && set->array[pp->id];
}

bool pp_set_equals(struct pp_set *x, struct pp_set *y)
{
	for (unsigned int i = 0; i < MAX(x->capacity, y->capacity); i++) {
		if (i >= x->capacity && y->array[i]) {
			/* y is bigger than x and has some new pp in it */
			return false;
		} else if (i >= y->capacity && x->array[i]) {
			/* x is bigger than y and has some new pp in it */
			return false;
		} else if (x->array[i] ^ y->array[i]) {
			return false;
		}
	}
	return true;
}

bool pp_subset(struct pp_set *sub, struct pp_set *super)
{
	/* Does 'sub' have any PPs in it that 'super' doesn't? */
	for (unsigned int i = 0; i < sub->capacity; i++) {
		if (i >= super->capacity) {
			/* 'sub' was created later... */
			if (sub->array[i]) {
				/* ...and also had a later such pp enabled. */
				return false;
			}
		} else {
			if (sub->array[i] && !super->array[i]) {
				return false;
			}
		}
	}
	return true;
}

struct pp *pp_next(struct pp_set *set, struct pp *current)
{
	unsigned int next_index = current == NULL ? 0 : current->id + 1;
	if (next_index == set->capacity) {
		return NULL;
	}

	while (!set->array[next_index]) {
		next_index++;
		if (next_index == set->capacity) {
			return NULL;
		}
	}
	return pp_get(next_index);
}

unsigned int compute_generation(struct pp_set *set)
{
	struct pp *pp;
	unsigned int max_generation = 0;
	FOR_EACH_PP(pp, set) {
		if (pp->generation >= max_generation) {
			max_generation = pp->generation + 1;
		}
	}
	return max_generation;
}

void record_explored_pps(struct pp_set *set)
{
	struct pp *pp;
	/* nb. iteration takes the read lock */
	FOR_EACH_PP(pp, set) {
		/* strictly speaking the lock is not needed to protect the
		 * explored flag, as it's write-once. */
		WRITE_LOCK(&pp_registry_lock);
		pp->explored = true;
		RW_UNLOCK(&pp_registry_lock);
	}
}

/* output may change across subsequent calls because of other threads.
 * returns NULL, *not* an empty set, if there were no unexplored PPs. */
struct pp_set *filter_unexplored_pps(struct pp_set *set)
{
	struct pp_set *new_set = clone_pp_set(set);
	struct pp *pp;
	bool any = false;
	/* filter (lambda pp. !pp->explored) set */
	FOR_EACH_PP(pp, new_set) {
		READ_LOCK(&pp_registry_lock);
		if (pp->explored) {
			new_set->array[pp->id] = false;
		} else {
			any = true;
		}
		RW_UNLOCK(&pp_registry_lock);
	}

	if (!any) {
		free_pp_set(new_set);
		return NULL;
	} else {
		return new_set;
	}
}

/* returns PRIORITY_ALL if no pps are unexplored in a nonempty set. */
unsigned int unexplored_priority(struct pp_set *set)
{
	struct pp *pp;
	unsigned int min = PRIORITY_ALL;
	bool emptyset = true;
	/* min $ map (lambda pp. pp->priority) $ filter_unexplored_pps set */
	FOR_EACH_PP(pp, set) {
		emptyset = false;
		READ_LOCK(&pp_registry_lock);
		if (!pp->explored && pp->priority < min) {
			min = pp->priority;
		}
		RW_UNLOCK(&pp_registry_lock);
	}
	if (emptyset) {
		return PRIORITY_NONE;
	} else {
		return min;
	}
}
