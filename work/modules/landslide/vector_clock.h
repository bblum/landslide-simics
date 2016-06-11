/**
 * @file vector_clock.h
 * @brief Data structures for more different data race detection
 * @author Ben Blum
 */

#ifndef __LS_VECTOR_CLOCK_H
#define __LS_VECTOR_CLOCK_H

#include "array_list.h"
#include "common.h"
#include "rbtree.h"

struct epoch {
	unsigned int tid;
	unsigned int timestamp;
};

struct vector_clock {
	ARRAY_LIST(struct epoch) v;
};

/* The global set of all vector clocks associated with each mutex/xchg.
 * Corresponds to "L" in the fasttrack paper. Stored in sched_state.
 * C, W, and R are stored individually in the agent and mem_access strux. */
struct lock_clocks {
	struct rb_root map;
	unsigned int num_lox;
};

void vc_init(struct vector_clock *vc);
void vc_copy(struct vector_clock *vc_new, const struct vector_clock *vc_existing);
#define vc_destroy(vc) do { ARRAY_LIST_FREE(&(vc)->v); } while (0)
void vc_inc(struct vector_clock *vc, unsigned int tid);
unsigned int vc_get(struct vector_clock *vc, unsigned int tid);
void vc_merge(struct vector_clock *vc_dest, struct vector_clock *vc_src);
bool vc_eq(struct vector_clock *vc1, struct vector_clock *vc2);
bool vc_happens_before(struct vector_clock *vc_before, struct vector_clock *vc_after);
void vc_print(verbosity v, struct vector_clock *vc);

void lock_clocks_init(struct lock_clocks *lm);
void lock_clocks_copy(struct lock_clocks *lm_new, const struct lock_clocks *lm_existing);
void lock_clocks_destroy(struct lock_clocks *lm);
bool lock_clock_find(struct lock_clocks *lc, unsigned int lock_addr, struct vector_clock **result);
struct vector_clock *lock_clock_get(struct lock_clocks *lc, unsigned int lock_addr);
void lock_clock_set(struct lock_clocks *lc, unsigned int lock_addr, struct vector_clock *vc);

/* FT-acquire, see fasttrack paper */
#define VC_ACQUIRE(lc, current_clock, lock_addr) do {			\
		struct vector_clock *__clock;				\
		if (lock_clock_find((lc), (lock_addr), &__clock)) {	\
			vc_merge((current_clock), __clock);		\
		}							\
	} while (0)

/* FT-release, see fasttrack paper */
#define VC_RELEASE(lc, current_clock, tid, lock_addr) do {		\
		struct vector_clock *__clock = (current_clock);		\
		lock_clock_set((lc), (lock_addr), __clock);		\
		vc_inc(__clock, (tid));					\
	} while (0)

#endif
