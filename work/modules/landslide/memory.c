/**
 * @file memory.c
 * @brief routines for tracking dynamic allocations and otherwise shared memory
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <assert.h>
#include <simics/api.h>

#define MODULE_NAME "MEMORY"
#define MODULE_COLOUR COLOUR_DARK COLOUR_YELLOW

#include "common.h"
#include "found_a_bug.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "memory.h"
#include "rbtree.h"
#include "tree.h"

/******************************************************************************
 * Heap helpers
 ******************************************************************************/

/* Returns NULL (and sets chunk) if a chunk containing addr (i.e., base <= addr
 * && addr < base + len) already exists.
 * Does not set parent if the heap is empty. */
static struct rb_node **find_insert_location(struct rb_root *root, int addr,
					     struct chunk **chunk)
{
	struct rb_node **p = &root->rb_node;

	while (*p) {
		*chunk = rb_entry(*p, struct chunk, nobe);

		/* branch left */
		if (addr < (*chunk)->base) {
			p = &(*p)->rb_left;
		/* does this chunk contain addr? */
		} else if (addr < (*chunk)->base + (*chunk)->len) {
			return NULL;
		/* branch right */
		} else {
			p = &(*p)->rb_right;
		}
	}

	return p;
}

/* finds the chunk with the nearest start address lower than this address.
 * if the address is lower than anything currently in the heap, returns null. */
static struct chunk *find_containing_chunk(struct rb_root *root, int addr)
{
	struct chunk *target = NULL;
	struct rb_node **p = find_insert_location(root, addr, &target);

	if (p == NULL) {
		return target;
	} else {
		return NULL;
	}
}

static void insert_chunk(struct rb_root *root, struct chunk *c, bool coalesce)
{
	struct chunk *parent = NULL;
	struct rb_node **p = find_insert_location(root, c->base, &parent);

	// XXX: If inserting [x|y] into a heap that has [z|w] with x<z<x+y,
	// find_insert_location will have no clue. I can't imagine that this
	// would cause any sort of bug, though...
	if (coalesce && p == NULL) {
		assert(parent != NULL);
		parent->len = MAX(parent->len, c->len + c->base - parent->base);
		MM_FREE(c);
		return;
	}

	assert(p != NULL && "allocated a block already contained in the heap?");

	rb_init_node(&c->nobe);
	rb_link_node(&c->nobe, parent != NULL ? &parent->nobe : NULL, p);
	rb_insert_color(&c->nobe, root);
}

static struct chunk *remove_chunk(struct rb_root *root, int addr, int len)
{
	struct chunk *target = NULL;
	struct rb_node **p = find_insert_location(root, addr, &target);

	if (p == NULL) {
		assert(target != NULL);
		//assert(target->base == addr && target->len == len);
		rb_erase(&target->nobe, root);
		return target;
	} else {
		/* no containing block found */
		return NULL;
	}
}

static void print_heap(verbosity v, struct rb_node *nobe, bool rightmost)
{
	if (nobe == NULL)
		return;

	struct chunk *c = rb_entry(nobe, struct chunk, nobe);

	print_heap(v, c->nobe.rb_left, false);
	printf(v, "[0x%x | %d]", c->base, c->len);
	if (!rightmost || c->nobe.rb_right != NULL)
		printf(v, ", ");
	print_heap(v, c->nobe.rb_right, rightmost);
}

/******************************************************************************
 * shm helpers
 ******************************************************************************/

#define MEM_ENTRY(rb) \
	((rb) == NULL ? NULL : rb_entry(rb, struct mem_access, nobe))

static void add_shm(struct ls_state *ls, struct mem_state *m, struct chunk *c,
		    int addr, bool write)
{
	struct rb_node **p = &m->shm.rb_node;
	struct rb_node *parent = NULL;
	struct mem_access *ma;

	while (*p) {
		parent = *p;
		ma = MEM_ENTRY(parent);

		if (addr < ma->addr) {
			p = &(*p)->rb_left;
		} else if (addr > ma->addr) {
			p = &(*p)->rb_right;
		} else {
			/* access already exists */
			ma->count++;
			ma->write = ma->write || write;
			return;
		}
	}

	/* doesn't exist; create a new one */
	ma = MM_XMALLOC(1, struct mem_access);
	ma->addr      = addr;
	ma->write     = write;
	ma->eip       = ls->eip;
	ma->count     = 1;
	ma->conflict  = false;
	ma->other_tid = 0;

#ifndef STUDENT_FRIENDLY
	if (c != NULL) {
		kern_address_other_kstack(ls->cpu0, addr, c->base, c->len,
					  &ma->other_tid);
	}
#endif

	rb_link_node(&ma->nobe, parent, p);
	rb_insert_color(&ma->nobe, &m->shm);
}

/******************************************************************************
 * Interface
 ******************************************************************************/

void mem_init(struct mem_state *m)
{
	m->heap.rb_node = NULL;
	m->heap_size = 0;
	m->guest_init_done = false;
	m->in_alloc = false;
	m->in_free = false;
	m->shm.rb_node = NULL;
	m->freed.rb_node = NULL;
}

/* heap interface */

/* bad place == mal loc */
static void mem_enter_bad_place(struct ls_state *ls, struct mem_state *m,
				int size)
{
	if (m->in_alloc || m->in_free) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "Malloc reentered %s!\n",
			 m->in_alloc ? "Malloc" : "Free");
		found_a_bug(ls);
	}

	m->in_alloc = true;
	m->alloc_request_size = size;
}

static void mem_exit_bad_place(struct ls_state *ls, struct mem_state *m,
			       int base)
{
	assert(m->in_alloc && "attempt to exit malloc without being in!");
	assert(!m->in_free && "attempt to exit malloc while in free!");

	lsprintf(DEV, "Malloc [0x%x | %d]\n", base, m->alloc_request_size);
	assert(base < USER_MEM_START);

	if (base == 0) {
		lsprintf(INFO, "Kernel seems to be out of memory.\n");
	} else {
		struct chunk *chunk = MM_XMALLOC(1, struct chunk);
		chunk->base = base;
		chunk->len = m->alloc_request_size;
		chunk->malloc_trace = stack_trace(ls->cpu0, ls->eip,
						  ls->sched.cur_agent->tid);
		chunk->free_trace = NULL;

		m->heap_size += m->alloc_request_size;
		insert_chunk(&m->heap, chunk, false);
	}

	m->in_alloc = false;
}

static void mem_enter_free(struct ls_state *ls, struct mem_state *m, int base,
			   int size)
{
	struct chunk *chunk;

	if (m->in_alloc || m->in_free) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "Free reentered %s!\n",
			 m->in_alloc ? "Malloc" : "Free");
		found_a_bug(ls);
	}

	chunk = remove_chunk(&m->heap, base, size);

	if (base == 0) {
		assert(chunk == NULL);
		lsprintf(INFO, "Free() NULL; ok, I guess...\n");
	} else if (chunk == NULL) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "Attempted to free non-existent chunk [0x%x | %d]"
			 " -- (double free?)!\n", base, size);
		found_a_bug(ls);
	} else if (chunk->base != base) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "Attempted to free [0x%x | %d], contained within "
			 "[0x%x | %d]\n", base, size, chunk->base, chunk->len);
		found_a_bug(ls);
	} else if (chunk->len != size) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "Attempted to free [0x%x | %d] with wrong size %d!\n",
			 base, chunk->len, size);
		found_a_bug(ls);
	} else {
		lsprintf(DEV, "Free() chunk [0x%x | %d]\n", base, size);
	}

	if (chunk != NULL) {
		m->heap_size -= chunk->len;
		assert(chunk->free_trace == NULL);
		chunk->free_trace = stack_trace(ls->cpu0, ls->eip,
						ls->sched.cur_agent->tid);
		insert_chunk(&m->freed, chunk, true);
	}

	m->in_free = true;
}

static void mem_exit_free(struct ls_state *ls, struct mem_state *m)
{
	assert(m->in_free && "attempt to exit free without being in!");
	assert(!m->in_alloc && "attempt to exit free while in malloc!");
	m->in_free = false;
}

void mem_update(struct ls_state *ls)
{
	/* Dynamic memory allocation tracking */
	int size;
	int base;

	/* Only start tracking allocations after kernel_main is entered - the
	 * multiboot code that runs before kernel_main may confuse us. */
	if (!ls->mem.guest_init_done) {
		if (kern_kernel_main(ls->eip)) {
			ls->mem.guest_init_done = true;
		} else {
			return;
		}
	}

	if (within_function(ls->cpu0, ls->eip, GUEST_LMM_REMOVE_FREE_ENTER,
			    GUEST_LMM_REMOVE_FREE_EXIT))
		return;

	if (kern_lmm_alloc_entering(ls->cpu0, ls->eip, &size)) {
		mem_enter_bad_place(ls, &ls->mem, size);
	} else if (kern_lmm_alloc_exiting(ls->cpu0, ls->eip, &base)) {
		mem_exit_bad_place(ls, &ls->mem, base);
	} else if (kern_lmm_free_entering(ls->cpu0, ls->eip, &base, &size)) {
		mem_enter_free(ls, &ls->mem, base, size);
	} else if (kern_lmm_free_exiting(ls->eip)) {
		mem_exit_free(ls, &ls->mem);
	}
}

/* shm interface */

#define BUF_SIZE 256

/* Attempt to find a freed chunk among all transitions */
static struct chunk *find_freed_chunk(struct ls_state *ls, int addr,
				      struct hax **before, struct hax **after)
{
	struct mem_state *m = &ls->mem;
	*before = NULL;
	*after = ls->save.current;

	do {
		struct chunk *c = find_containing_chunk(&m->freed, addr);
		if (c != NULL) {
			assert(c->malloc_trace != NULL);
			assert(c->free_trace != NULL);
			return c;
		}

		/* Walk up the choice tree branch */
		*before = *after;
		if (*after != NULL) {
			*after = (*after)->parent;
			m = (*before)->oldmem;
		}
	} while (*before != NULL);

	return NULL;
}

static void use_after_free(struct ls_state *ls, struct mem_state *m, int addr,
			   bool write)
{
	lsprintf(BUG, COLOUR_BOLD COLOUR_RED "USE AFTER FREE - %s 0x%.8x at eip"
		 " 0x%.8x\n", write ? "write to" : "read from", addr,
		 (int)GET_CPU_ATTR(ls->cpu0, eip));
	lsprintf(BUG, "Heap contents: {");
	print_heap(BUG, m->heap.rb_node, true);
	printf(BUG, "}\n");

	/* Find the chunk and print stack traces for it */
	struct hax *before;
	struct hax *after;
	char before_buf[BUF_SIZE];
	char after_buf[BUF_SIZE];
	struct chunk *c = find_freed_chunk(ls, addr, &before, &after);

	if (c == NULL) {
		lsprintf(BUG, "0x%x was never allocated...\n", addr);
		found_a_bug(ls);
		return;
	}

	if (after == NULL) {
		snprintf(after_buf, BUF_SIZE, "<root>");
	} else {
		snprintf(after_buf, BUF_SIZE, "#%d/tid%d", after->depth,
			 after->chosen_thread);
	}
	if (before == NULL) {
		snprintf(before_buf, BUF_SIZE, "<current>");
	} else {
		snprintf(before_buf, BUF_SIZE, "#%d/tid%d", before->depth,
			 before->chosen_thread);
	}

	lsprintf(BUG, "[0x%x | %d] was allocated by %s\n",
		 c->base, c->len, c->malloc_trace);
	lsprintf(BUG, "...and, between choices %s and %s, freed by %s\n",
		 after_buf, before_buf, c->free_trace);

	found_a_bug(ls);
}

void mem_check_shared_access(struct ls_state *ls, struct mem_state *m, int addr,
			     bool write)
{
	if (!ls->sched.guest_init_done)
		return;

	/* the allocator has a free pass */
	if (m->in_alloc || m->in_free)
		return;

	/* so does the scheduler - TODO: make this configurable */
	if (kern_in_scheduler(ls->cpu0, ls->eip) ||
	    kern_access_in_scheduler(addr) ||
	    ls->sched.cur_agent->action.handling_timer || /* XXX: a hack */
	    ls->sched.cur_agent->action.context_switch)
		return;

#ifndef STUDENT_FRIENDLY
	/* ignore certain "probably innocent" accesses */
	if (kern_address_own_kstack(ls->cpu0, addr))
		return;
#endif

	if (kern_address_in_heap(addr)) {
		struct chunk *c = find_containing_chunk(&m->heap, addr);
		if (c == NULL) {
			use_after_free(ls, m, addr, write);
		} else {
			add_shm(ls, m, c, addr, write);
		}
	} else if (kern_address_global(addr)) {
		add_shm(ls, m, NULL, addr, write);
	}
}

/* Remove the need for the student to implement kern_address_hint. */
#ifdef STUDENT_FRIENDLY
#define kern_address_hint(cpu, buf, size, addr, base, len) \
	snprintf(buf, size, "0x%.8x in [0x%x | %d]", addr, base, len)
#endif

static void print_shm_conflict(conf_object_t *cpu,
			       struct mem_state *m0, struct mem_state *m1,
			       struct mem_access *ma0, struct mem_access *ma1)
{
	char buf[BUF_SIZE];
	struct chunk *c0 = find_containing_chunk(&m0->heap, ma0->addr);
	struct chunk *c1 = find_containing_chunk(&m1->heap, ma1->addr);

	assert(ma0->addr == ma1->addr);

	if (c0 == NULL && c1 == NULL) {
		if (kern_address_in_heap(ma0->addr)) {
			/* This could happen if both transitions did a heap
			 * access, then did free() on the corresponding chunk
			 * before the next choice point. TODO: free() might
			 * itself be a good place to set choice points... */
			snprintf(buf, BUF_SIZE, "heap0x%.8x", ma0->addr);
		} else {
			/* Attempt to find its name in the symtable. */
			symtable_lookup_data(buf, BUF_SIZE, ma0->addr);
		}
	} else {
		if (c0 == NULL)
			c0 = c1; /* default to "later" state if both exist */
		/* If this happens, c0 and c1 were both not null and also got
		 * reallocated in-between. TODO: have better printing code.
		 * assert(c1 == NULL ||
		 *        (c0->base == c1->base && c0->len == c1->len));
		 */
		kern_address_hint(cpu, buf, BUF_SIZE, ma0->addr,
				  c0->base, c0->len);
	}
	printf(DEV, "[%s %c%d/%c%d]", buf, ma0->write ? 'w' : 'r', ma0->count,
	       ma1->write ? 'w' : 'r', ma1->count);
}

#define MAX_CONFLICTS 10
static void check_stack_conflict(struct mem_access *ma, int other_tid,
				 int *conflicts)
{
	/* The motivation for this function is that, as an optimisation, we
	 * don't record shm accesses to a thread's own stack. The flip-side of
	 * this is that if another thread accesses your stack, it is guaranteed
	 * to be a conflict, and also won't be recorded in your transitions. So
	 * we have to check every recorded access that doesn't match. */
	if (ma->other_tid == other_tid) {
		if (*conflicts < MAX_CONFLICTS) {
			if (*conflicts > 0)
				printf(DEV, ", ");
			printf(DEV, "[tid%d stack %c%d 0x%x]", other_tid,
			       ma->write ? 'w' : 'r', ma->count, ma->eip);
		}
		ma->conflict = true;
		(*conflicts)++;
	}
}

static void check_freed_conflict(conf_object_t *cpu, struct mem_access *ma0,
				 struct mem_state *m1, int other_tid,
				 int *conflicts)
{
	struct chunk *c = find_containing_chunk(&m1->freed, ma0->addr);

	if (c != NULL) {
		char buf[BUF_SIZE];
		kern_address_hint(cpu, buf, BUF_SIZE, ma0->addr,
				  c->base, c->len);

		if (*conflicts < MAX_CONFLICTS) {
			if (*conflicts > 0)
				printf(DEV, ", ");
			printf(DEV, "[%s %c%d (tid%d freed)]", buf, ma0->write ? 'w' : 'r',
			       ma0->count, other_tid);
		}
		ma0->conflict = true;
		(*conflicts)++;
	}
}

/* Compute the intersection of two transitions' shm accesses */
bool mem_shm_intersect(conf_object_t *cpu, struct mem_state *m0,
		       struct mem_state *m1, int depth0, int tid0,
		       int depth1, int tid1)
{
	struct mem_access *ma0 = MEM_ENTRY(rb_first(&m0->shm));
	struct mem_access *ma1 = MEM_ENTRY(rb_first(&m1->shm));
	int conflicts = 0;

	lsprintf(DEV, "Intersecting transition %d (TID %d) with %d (TID %d): {",
		 depth0, tid0, depth1, tid1);

	while (ma0 != NULL && ma1 != NULL) {
		if (ma0->addr < ma1->addr) {
			check_stack_conflict(ma0, tid1, &conflicts);
			check_freed_conflict(cpu, ma0, m1, tid1, &conflicts);
			/* advance ma0 */
			ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
		} else if (ma0->addr > ma1->addr) {
			check_stack_conflict(ma1, tid0, &conflicts);
			check_freed_conflict(cpu, ma1, m0, tid0, &conflicts);
			/* advance ma1 */
			ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
		} else {
			/* found a match; advance both */
			if (ma0->write || ma1->write) {
				if (conflicts < MAX_CONFLICTS) {
					if (conflicts > 0)
						printf(DEV, ", ");
					/* the match is also a conflict */
					print_shm_conflict(cpu, m0, m1,
							   ma0, ma1);
				}
				conflicts++;
				ma0->conflict = true;
				ma1->conflict = true;
			}
			ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
			ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
		}
	}

	/* even if one transition runs out of recorded accesses, we still need
	 * to check the other one's remaining accesses for the one's stack. */
	while (ma0 != NULL) {
		check_stack_conflict(ma0, tid1, &conflicts);
		check_freed_conflict(cpu, ma0, m1, tid1, &conflicts);
		ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
	}
	while (ma1 != NULL) {
		check_stack_conflict(ma1, tid0, &conflicts);
		check_freed_conflict(cpu, ma1, m0, tid0, &conflicts);
		ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
	}

	if (conflicts > MAX_CONFLICTS)
		printf(DEV, ", and %d more", conflicts - MAX_CONFLICTS);
	printf(DEV, "}\n");

	return conflicts > 0;
}
