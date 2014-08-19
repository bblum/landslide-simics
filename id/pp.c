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

/* global state */
pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;
static unsigned int next_id; /* also represents 1 + max legal index */
static unsigned int max_generation = 0;
static unsigned int capacity;
static struct pp **registry = NULL; /* NULL means not yet initialized */

#define INITIAL_CAPACITY 16

#define READ_LOCK() do {					\
		int __ret = pthread_rwlock_rdlock(&lock);	\
		assert(__ret == 0 && "failed rdlock");		\
	} while (0)

#define WRITE_LOCK() do {					\
		int __ret = pthread_rwlock_wrlock(&lock);	\
		assert(__ret == 0 && "failed wrlock");		\
	} while (0)

#define UNLOCK() do {						\
		int __ret = pthread_rwlock_unlock(&lock);	\
		assert(__ret == 0 && "failed unlock");		\
	} while (0)

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
	READ_LOCK();
	bool need_init = registry == NULL;
	UNLOCK();
	if (need_init) {
		WRITE_LOCK();
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
		UNLOCK();
	}
}

struct pp *pp_new(char *config_str, unsigned int priority, unsigned int generation)
{
	check_init();
	WRITE_LOCK();
	struct pp *result = pp_append(config_str, priority, generation);
	UNLOCK();
	return result;
}

struct pp *pp_get(unsigned int id)
{
	check_init();
	READ_LOCK();
	assert(id < next_id && "nonexistent pp of that id");
	struct pp *result = registry[id];
	UNLOCK();
	return result;
}

// TODO: write_pp_set_to_config_fd
