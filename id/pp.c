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
static unsigned int capacity;
static struct pp *registry = NULL; /* NULL means not yet initialized */

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

static unsigned int pp_append(char *config_str, unsigned int priority)
{
	if (next_id == capacity) {
		assert(capacity < UINT_MAX / 2);
		struct pp *resized = XMALLOC(capacity * 2, struct pp);
		memcpy(resized, registry, next_id * sizeof(struct pp));
		FREE(registry);
		registry = resized;
		capacity *= 2;
	}
	registry[next_id].config_str = config_str;
	registry[next_id].priority   = priority;
	next_id++;
	return next_id - 1;
}

static void check_init() {
	READ_LOCK();
	bool need_init = registry == NULL;
	UNLOCK();
	if (need_init) {
		WRITE_LOCK();
		if (registry == NULL) { /* DCL */
			capacity = 16;
			next_id = 0;
			registry = XMALLOC(capacity, struct pp);
			unsigned int id = pp_append(
				XSTRDUP("within_function mutex_lock"),
				PRIORITY_MUTEX_LOCK);
			assert(id == 0);
			id = pp_append(
				XSTRDUP("within_function mutex_unlock"),
				PRIORITY_MUTEX_UNLOCK);
			assert(id == 1);
			assert(next_id == 2);
		}
		UNLOCK();
	}
}

unsigned int pp_new(char *config_str, unsigned int priority)
{
	check_init();
	WRITE_LOCK();
	unsigned int result = pp_append(config_str, priority);
	UNLOCK();
	return result;
}

/* the struct pp pointers may be relocated when the array gets resized, but
 * the contained pointers (to config_str) are guaranteed to stay valid */
void pp_get(unsigned int id, char **config_str, unsigned int *priority)
{
	check_init();
	READ_LOCK();
	assert(id < next_id && "nonexistent pp of that id");
	*config_str = registry[id].config_str;
	*priority   = registry[id].priority;
	UNLOCK();
}

// TODO: write_pp_set_to_config_fd
