/**
 * @file pp.c
 * @brief preemption points
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <pthread.h>
#include <limits.h>

#include "common.h"
#include "pp.h"
#include "sync.h"

/* global state */
static pthread_rwlock_t pp_registry_lock = PTHREAD_RWLOCK_INITIALIZER;
static unsigned int next_id; /* also represents 1 + max legal index */
static unsigned int max_generation = 0;
static unsigned int capacity;
static struct pp **registry = NULL; /* NULL means not yet initialized */

#define INITIAL_CAPACITY 16

/******************************************************************************
 * PP registry
 ******************************************************************************/

static struct pp *pp_append(char *config_str, unsigned int priority,
			    unsigned int generation)
{
	struct pp *pp = XMALLOC(1, struct pp);
	pp->config_str = config_str;
	pp->priority   = priority;
	pp->id         = next_id;
	pp->generation = generation;

	if (generation > max_generation) {
		// TODO: have a printing framework
		max_generation = generation;
	}

	// TODO: convert to use array-list macross
	if (next_id == capacity) {
		assert(capacity < UINT_MAX / 2);
		struct pp **resized = XMALLOC(capacity * 2, struct pp *);
		memcpy(resized, registry, next_id * sizeof(struct pp *));
		FREE(registry);
		registry = resized;
		capacity *= 2;
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
			capacity = INITIAL_CAPACITY;
			next_id = 0;
			registry = XMALLOC(capacity, struct pp *);
			struct pp *pp = pp_append(
				XSTRDUP("within_function mutex_lock"),
				PRIORITY_MUTEX_LOCK, max_generation);
			assert(pp->id == 0);
			pp = pp_append(
				XSTRDUP("within_function mutex_unlock"),
				PRIORITY_MUTEX_UNLOCK, max_generation);
			assert(pp->id == 1);
			assert(next_id == 2);
		}
		RW_UNLOCK(&pp_registry_lock);
	}
}

struct pp *pp_new(char *config_str, unsigned int priority, unsigned int generation)
{
	check_init();
	WRITE_LOCK(&pp_registry_lock);
	struct pp *result = pp_append(config_str, priority, generation);
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

/******************************************************************************
 * PP sets
 ******************************************************************************/

struct pp_set *create_pp_set(unsigned int pp_mask)
{
	check_init();
	READ_LOCK(&pp_registry_lock);
	unsigned int size = sizeof(struct pp_set) + (sizeof(bool) * next_id);
	struct pp_set *set = (struct pp_set *)XMALLOC(size, char /* c.c */);
	set->len = next_id;
	for (unsigned int i = 0; i < next_id; i++) {
		set->array[i] = ((pp_mask & pp_get(i)->priority) != 0);
	}
	RW_UNLOCK(&pp_registry_lock);
	return set;
}

void free_pp_set(struct pp_set *set)
{
	FREE(set);
}

struct pp *pp_next(struct pp_set *set, struct pp *current)
{
	unsigned int next_index = current == NULL ? 0 : current->id + 1;
	if (next_index == set->len) {
		return NULL;
	}

	while (!set->array[next_index]) {
		next_index++;
		if (next_index == set->len) {
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
