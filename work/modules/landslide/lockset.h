/**
 * @file lockset.h
 * @brief Data structures for data race detection
 * @author Ben Blum
 */

#ifndef __LS_LOCKSET_H
#define __LS_LOCKSET_H

#include  "common.h"

struct sched_state;

struct lock {
	int addr;
	bool write; /* 1 always for mutexes; 1 or 0 for rwlocks */
};

/* Tracks the locks held by a given thread, for data race detection. */

// using an array for simplicity of implementation, sorry! it would not be
// hard to use a proper set structure, but i'm doing research here.
#define MAX_LOCKS 256
struct lockset {
	int num_locks;
	int capacity;
	struct lock *locks;
};

// For efficient storage of locksets on memory accesses.
enum lockset_cmp_result {
	LOCKSETS_EQ,     // locksets contain all same elements
	LOCKSETS_SUBSET, // l1 subset l2
	LOCKSETS_SUPSET, // l2 subset l1
	LOCKSETS_DIFF    // locksets contain some disjoint locks
};

void lockset_init(struct lockset *l);
void lockset_free(struct lockset *l);
void lockset_print(verbosity v, struct lockset *l);
void lockset_clone(struct lockset *dest, struct lockset *src);
void lockset_add(struct lockset *l, int lock_addr, bool write);
void lockset_remove(struct sched_state *s, int lock_addr, bool in_kernel);
bool lockset_intersect(struct lockset *l1, struct lockset *l2);
enum lockset_cmp_result lockset_compare(struct lockset *l1, struct lockset *l2);

#endif
