/**
 * @file memory.h
 * @brief routines for tracking dynamic allocations and otherwise shared memory
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_MEMORY_H
#define __LS_MEMORY_H

#include <simics/api.h> /* for bool, of all things... */

#include "rbtree.h"

/******************************************************************************
 * Shared memory access tracking
 ******************************************************************************/

/* represents an access to shared memory */
struct mem_access {
	int addr;      /* byte granularity */
	bool write;    /* false == read; true == write */
	int count;     /* how many times accessed? (stats) */
	bool conflict; /* does this conflict with another transition? (stats) */
	struct rb_node nobe;
};

/******************************************************************************
 * Heap state tracking
 ******************************************************************************/

/* a heap-allocated block. */
struct chunk {
	int base;
	int len;
	struct rb_node nobe;
};

struct mem_state {
	struct rb_root heap;
	/* dynamic allocation request state */
	bool in_alloc;
	bool in_free;
	int alloc_request_size; /* valid iff in_alloc */
	/* set of all shared accesses that happened during this transition;
	 * cleared after each save point - done in save.c */
	struct rb_root shm;
};

/******************************************************************************
 * Interface
 ******************************************************************************/

void mem_init(struct mem_state *);

void mem_enter_bad_place(struct ls_state *, struct mem_state *, int size);
void mem_exit_bad_place(struct ls_state *, struct mem_state *, int base);
void mem_enter_free(struct ls_state *, struct mem_state *, int base, int size);
void mem_exit_free(struct ls_state *, struct mem_state *);

void mem_check_shared_access(struct ls_state *, struct mem_state *, int addr,
			     bool write);
bool mem_shm_intersect(struct mem_state *, struct mem_state *, int, int);

#endif
