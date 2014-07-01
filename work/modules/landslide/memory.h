/**
 * @file memory.h
 * @brief routines for tracking dynamic allocations and otherwise shared memory
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_MEMORY_H
#define __LS_MEMORY_H

#include <simics/api.h> /* for bool, of all things... */

#include "lockset.h"
#include "rbtree.h"

struct hax;
struct stack_trace;

/******************************************************************************
 * Shared memory access tracking
 ******************************************************************************/

struct mem_lockset {
	int eip;
	struct lockset locks_held;
	Q_NEW_LINK(struct mem_lockset) nobe;
};

Q_NEW_HEAD(struct mem_locksets, struct mem_lockset);

/* represents an access to shared memory */
struct mem_access {
	int addr;      /* byte granularity */
	bool write;    /* false == read; true == write */
	/* PC is recorded per-lockset, so when there's a data race, the correct
	 * eip can be reported instead of the first one. */
	//int eip;       /* what instruction pointer */
	int other_tid; /* does this access another thread's stack? 0 if none */
	int count;     /* how many times accessed? (stats) */
	bool conflict; /* does this conflict with another transition? (stats) */
	struct mem_locksets locksets; /* distinct locksets used while accessing */
	struct rb_node nobe;
};

/* represents two instructions by different threads which accessed the same
 * memory location, where both threads did not hold the same lock and there was
 * not a happens-before relation between them [insert citation here]. the
 * intuition behind these criteria is that the memory accesses are "concurrent"
 * and can be interleaved at instruction granularity. in iterative deepening,
 * we use data race reports to identify places for new preemption points.
 *
 * a data race may be either "suspected" or "confirmed", the distinction being
 * whether we have yet observed that the instruction pair can actually be
 * reordered. in some cases a "suspected" data race cannot actually be reordered
 * because of an implicit HB relationship that's established through some other
 * shared variable communication. these are invisible to us during single-branch
 * analysis, because we compute HB only by runqueues, and can't see how shm
 * communication might affect flow control. fortunately, unlike single-branch
 * detectors (eraser, tsan, go test -race), we can remember data race reports
 * cross-branch and suppress false positives by confirming the absence of such
 * implicit HB relations. it should be clear that this reduction is sound. */
struct data_race {
	int first_eip;
	int other_eip;
	/* which order were they observed in? "confirmed" iff both are true. */
	bool first_before_other;
	bool other_before_first;
	// TODO: record stack traces?
	// TODO: record originating tids (even when stack unavailable)?
	struct rb_node nobe;
};

/******************************************************************************
 * Heap state tracking
 ******************************************************************************/

/* a heap-allocated block. */
struct chunk {
	int base;
	int len;
	int id; /* distinguishes chunks in same-space-different-time */
	struct rb_node nobe;
	/* for use-after-free reporting */
	struct stack_trace *malloc_trace;
	struct stack_trace *free_trace;
};

struct mem_state {
	/**** heap state tracking ****/
	struct rb_root heap;
	int heap_size;
	int heap_next_id; /* generation counter for chunks */
	/* dynamic allocation request state */
	bool guest_init_done;
	bool in_mm_init; /* userspace only */
	bool in_alloc;
	bool in_free;
	int alloc_request_size; /* valid iff in_alloc */

	/**** userspace information ****/
	int cr3; /* 0 == uninitialized or this is for kernel mem */
	int cr3_tid; /* tid for which cr3 was registered (main tid of process) */
	int user_mutex_size; /* 0 == uninitialized or kernel mem as above */

	/**** shared memory conflict detection ****/
	/* set of all shared accesses that happened during this transition;
	 * cleared after each save point - done in save.c */
	struct rb_root shm;
	/* set of all chunks that were freed during this transition; cleared
	 * after each save point just like the shared memory one above */
	struct rb_root freed;
	/* set of candidate data races, maintained cross-branch */
	struct rb_root data_races;
};

/******************************************************************************
 * Interface
 ******************************************************************************/

void mem_init(struct ls_state *);

void mem_update(struct ls_state *);

void mem_check_shared_access(struct ls_state *, int phys_addr, int virt_addr,
							 bool write);
bool mem_shm_intersect(struct ls_state *ls, struct hax *h0, struct hax *h2,
                       bool in_kernel);

bool shm_contains_addr(struct mem_state *m, int addr);

bool check_user_address_space(struct ls_state *ls);

#endif
