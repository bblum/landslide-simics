/**
 * @file memory.h
 * @brief routines for tracking dynamic allocations and otherwise shared memory
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_MEMORY_H
#define __LS_MEMORY_H

#include <simics/api.h> /* for bool, of all things... */

#include "rbtree.h"

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
};

void mem_init(struct mem_state *);
void mem_enter_bad_place(struct ls_state *, struct mem_state *, int size);
void mem_exit_bad_place(struct ls_state *, struct mem_state *, int base);
void mem_enter_free(struct ls_state *, struct mem_state *, int base, int size);
void mem_exit_free(struct ls_state *, struct mem_state *);

#endif
