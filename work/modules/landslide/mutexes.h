/**
 * @file mutexes.h
 * @brief state for modeling userspace mutex behaviour
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#ifndef __LS_MUTEXES_H
#define __LS_MUTEXES_H

#include "variable_queue.h"

struct ls_state;

/* a dynamically-allocated part of a mutex. */
struct mutex_chunk {
	Q_NEW_LINK(struct mutex_chunk) nobe;
	unsigned int base;
	unsigned int size;
};

Q_NEW_HEAD(struct mutex_chunks, struct mutex_chunk);

/* a single mutex */
struct mutex {
	Q_NEW_LINK(struct mutex) nobe;
	unsigned int addr;
	struct mutex_chunks chunks;
};

Q_NEW_HEAD(struct mutexes, struct mutex);

/* the state of all mutexes known in userspace. */
struct mutex_state {
	int size;
	/* list of all known mutexes in userspace. note that mutexes are only
	 * placed on this list if mutex_init is observed to malloc. */
	struct mutexes user_mutexes;
};

void mutexes_init(struct mutex_state *m);

void learn_malloced_mutex_structure(struct mutex_state *m, int lock_addr,
									int chunk_addr, int chunk_size);

void mutex_destroy(struct mutex_state *m, int lock_addr);

void check_user_mutex_access(struct ls_state *ls, unsigned int addr);

#endif
