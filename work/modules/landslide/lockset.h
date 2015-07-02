/**
 * @file lockset.h
 * @brief Data structures for data race detection
 * @author Ben Blum
 */

#ifndef __LS_LOCKSET_H
#define __LS_LOCKSET_H

#include "array_list.h"
#include "common.h"

struct sched_state;

enum lock_type {
	LOCK_MUTEX,
	LOCK_SEM,
	LOCK_RWLOCK,
	LOCK_RWLOCK_READ,
};

#define SAME_LOCK_TYPE(t1, t2) \
	(((t1) == (t2)) || \
	 ((t1) == LOCK_RWLOCK && (t2) == LOCK_RWLOCK_READ) || \
	 ((t1) == LOCK_RWLOCK_READ && (t2) == LOCK_RWLOCK))

struct lock {
	unsigned int addr;
	enum lock_type type;
};

/* Tracks the locks held by a given thread, for data race detection. */
struct lockset {
	ARRAY_LIST(struct lock) list;
};

/* For efficient storage of locksets on memory accesses. */
enum lockset_cmp_result {
	LOCKSETS_EQ,     /* locksets contain all same elements */
	LOCKSETS_SUBSET, /* l0 subset l1 */
	LOCKSETS_SUPSET, /* l1 subset l0 */
	LOCKSETS_DIFF    /* locksets contain some disjoint locks */
};

void lockset_init(struct lockset *l);
void lockset_free(struct lockset *l);
void lockset_print(verbosity v, struct lockset *l);
void lockset_clone(struct lockset *dest, const struct lockset *src);
void lockset_record_semaphore(struct lockset *semaphores, unsigned int lock_addr,
			      bool is_semaphore);
void lockset_add(struct sched_state *s, struct lockset *l,
		 unsigned int lock_addr, enum lock_type type);
void lockset_remove(struct sched_state *s, unsigned int lock_addr,
		    enum lock_type type, bool in_kernel);
bool lockset_intersect(struct lockset *l0, struct lockset *l1);
enum lockset_cmp_result lockset_compare(struct lockset *l0, struct lockset *l1);

#endif
