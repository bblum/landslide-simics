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

/******************************************************************************
 * Heap helpers
 ******************************************************************************/

/* Returns NULL (and sets chunk) if a chunk containing addr (i.e., base <= addr
 * && addr < base + len) already exists.
 * Does not set parent if the heap is empty. */
static struct rb_node **find_insert_location(struct mem_state *m, int addr,
					     struct chunk **chunk)
{
	struct rb_node **p = &m->heap.rb_node;
	// struct chunk *c;

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
static struct chunk *find_containing_chunk(struct mem_state *m, int addr)
{
	struct chunk *target = NULL;
	struct rb_node **p = find_insert_location(m, addr, &target);

	if (p == NULL) {
		return target;
	} else {
		return NULL;
	}
}

static void new_chunk(struct mem_state *m, int addr, int len)
{
	struct chunk *parent = NULL;
	struct rb_node **p = find_insert_location(m, addr, &parent);

	assert(p != NULL && "allocated a block already contained in the heap?");

	struct chunk *c = MM_MALLOC(1, struct chunk);
	assert(c != NULL && "failed allocate chunk");
	c->base = addr;
	c->len = len;
	rb_init_node(&c->nobe);

	rb_link_node(&c->nobe, parent != NULL ? &parent->nobe : NULL, p);
	rb_insert_color(&c->nobe, &m->heap);
}

static struct chunk *remove_chunk(struct mem_state *m, int addr, int len)
{
	struct chunk *target = NULL;
	struct rb_node **p = find_insert_location(m, addr, &target);

	if (p == NULL) {
		assert(target != NULL);
		//assert(target->base == addr && target->len == len);
		rb_erase(&target->nobe, &m->heap);
		return target;
	} else {
		/* no containing block found */
		return NULL;
	}
}

static void print_heap(struct rb_node *nobe, bool rightmost)
{
	if (nobe == NULL)
		return;

	struct chunk *c = rb_entry(nobe, struct chunk, nobe);

	print_heap(c->nobe.rb_left, false);
	printf("[0x%x | %d]", c->base, c->len);
	if (!rightmost || c->nobe.rb_right != NULL)
		printf(", ");
	print_heap(c->nobe.rb_right, rightmost);
}

/******************************************************************************
 * shm helpers
 ******************************************************************************/

#define MEM_ENTRY(rb) \
	((rb) == NULL ? NULL : rb_entry(rb, struct mem_access, nobe))

static void add_shm(conf_object_t *cpu, struct mem_state *m, struct chunk *c,
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
	ma = MM_MALLOC(1, struct mem_access);
	assert(ma != NULL && "failed allocate mem_access");
	ma->addr      = addr;
	ma->write     = write;
	ma->count     = 1;
	ma->conflict  = false;
	ma->other_tid = 0;

	if (c != NULL) {
		kern_address_other_kstack(cpu, addr, c->base, c->len,
					  &ma->other_tid);
	}

	rb_link_node(&ma->nobe, parent, p);
	rb_insert_color(&ma->nobe, &m->shm);
}

/******************************************************************************
 * Interface
 ******************************************************************************/

void mem_init(struct mem_state *m)
{
	m->heap.rb_node = NULL;
	m->in_alloc = false;
	m->in_free = false;
	m->shm.rb_node = NULL;
}

/* heap interface */

/* bad place == mal loc */
void mem_enter_bad_place(struct ls_state *ls, struct mem_state *m, int size)
{
	if (m->in_alloc || m->in_free) {
		lsprintf("Malloc reentered %s!\n",
			 m->in_alloc ? "Malloc" : "Free");
		found_a_bug(ls);
	}

	m->in_alloc = true;
	m->alloc_request_size = size;
}

void mem_exit_bad_place(struct ls_state *ls, struct mem_state *m, int base)
{
	assert(m->in_alloc && "attempt to exit malloc without being in!");
	assert(!m->in_free && "attempt to exit malloc while in free!");
	new_chunk(m, base, m->alloc_request_size);
	lsprintf("Malloc chunk [0x%x | %d]\n", base, m->alloc_request_size);
	m->in_alloc = false;
}

void mem_enter_free(struct ls_state *ls, struct mem_state *m, int base, int size)
{
	struct chunk *chunk;

	if (m->in_alloc || m->in_free) {
		lsprintf("Free reentered %s!\n",
			 m->in_alloc ? "Malloc" : "Free");
		found_a_bug(ls);
	}

	chunk = remove_chunk(m, base, size);

	if (chunk == NULL) {
		lsprintf("Attempted to free non-existent chunk [0x%x | %d] "
			 "(double free?)!\n", base, size);
		found_a_bug(ls);
	} else if (chunk->base != base) {
		lsprintf("Attempted to free [0x%x | %d], contained within "
			 "[0x%x | %d]\n", base, size, chunk->base, chunk->len);
	} else if (chunk->len != size) {
		lsprintf("Attempted to free [0x%x | %d], but [0x%x | %d]!\n",
			 base, size, base, chunk->len);
		found_a_bug(ls);
	} else {
		lsprintf("Free() chunk [0x%x | %d]\n", base, size);
	}

	m->in_free = true;
}

void mem_exit_free(struct ls_state *ls, struct mem_state *m)
{
	assert(m->in_free && "attempt to exit free without being in!");
	assert(!m->in_alloc && "attempt to exit free while in malloc!");
	m->in_free = false;
}

/* shm interface */

void mem_check_shared_access(struct ls_state *ls, struct mem_state *m, int addr,
			     bool write)
{
	if (!ls->sched.guest_init_done)
		return;

	/* the allocator has a free pass */
	if (m->in_alloc || m->in_free)
		return;

	/* so does the scheduler - TODO: make this configurable */
	if (kern_in_scheduler(ls->eip) || kern_access_in_scheduler(addr))
		return;

	/* ignore certain "probably innocent" accesses */
	if (kern_address_own_kstack(ls->cpu0, addr))
		return;

	if (kern_address_in_heap(addr)) {
		struct chunk *c = find_containing_chunk(m, addr);
		if (c == NULL) {
			lsprintf("USE AFTER FREE - %s 0x%.8x at eip 0x%.8x\n",
				 write ? "write to" : "read from",
				 addr, ls->eip);
			lsprintf("Heap contents: {");
			print_heap(m->heap.rb_node, true);
			printf("}\n");
			found_a_bug(ls);
		} else {
			add_shm(ls->cpu0, m, c, addr, write);
		}
	} else if (kern_address_global(addr)) {
		add_shm(ls->cpu0, m, NULL, addr, write);
	}
}

#define BUF_SIZE 256

static void print_shm_conflict(conf_object_t *cpu,
			       struct mem_state *m0, struct mem_state *m1,
			       struct mem_access *ma0, struct mem_access *ma1)
{
	char buf[BUF_SIZE];
	struct chunk *c0 = find_containing_chunk(m0, ma0->addr);
	struct chunk *c1 = find_containing_chunk(m1, ma1->addr);

	assert(ma0->addr == ma1->addr);

	if (c0 == NULL && c1 == NULL) {
		if (kern_address_in_heap(ma0->addr)) {
			/* This could happen if both transitions did a heap
			 * access, then did free() on the corresponding chunk
			 * before the next choice point. TODO: free() might
			 * itself be a good place to set choice points... */
			snprintf(buf, BUF_SIZE, "heap0x%.8x", ma0->addr);
		} else {
			snprintf(buf, BUF_SIZE, "global0x%.8x", ma0->addr);
		}
	} else {
		if (c0 == NULL)
			c0 = c1; /* default to "later" state if both exist */
		assert(c1 == NULL ||
		       (c0->base == c1->base && c0->len == c1->len &&
		       "If this trips, replace with real code for this case"));
		kern_address_hint(cpu, buf, BUF_SIZE, ma0->addr,
				  c0->base, c0->len);
	}
	printf("[%s %c%d/%c%d]", buf, ma0->write ? 'w' : 'r', ma0->count,
	       ma1->write ? 'w' : 'r', ma1->count);
}

static void check_stack_conflict(struct mem_access *ma, int other_tid,
				 bool *conflict)
{
	/* The motivation for this function is that, as an optimisation, we
	 * don't record shm accesses to a thread's own stack. The flip-side of
	 * this is that if another thread accesses your stack, it is guaranteed
	 * to be a conflict, and also won't be recorded in your transitions. So
	 * we have to check every recorded access that doesn't match. */
	if (ma->other_tid == other_tid) {
		if (*conflict)
			printf(", ");
		ma->conflict = true;
		*conflict = true;
		printf("[tid%d stack %c%d]", other_tid, ma->write ? 'w' : 'r',
		       ma->count);
	}
}

/* Compute the intersection of two transitions' shm accesses */
bool mem_shm_intersect(conf_object_t *cpu, struct mem_state *m0,
		       struct mem_state *m1, int depth0, int tid0,
		       int depth1, int tid1)
{
	struct mem_access *ma0 = MEM_ENTRY(rb_first(&m0->shm));
	struct mem_access *ma1 = MEM_ENTRY(rb_first(&m1->shm));
	bool conflict = false;

	lsprintf("Intersecting transition %d (TID %d) with %d (TID %d): {",
		 depth0, tid0, depth1, tid1);

	while (ma0 != NULL && ma1 != NULL) {
		if (ma0->addr < ma1->addr) {
			check_stack_conflict(ma0, tid1, &conflict);
			/* advance ma0 */
			ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
		} else if (ma0->addr > ma1->addr) {
			check_stack_conflict(ma1, tid0, &conflict);
			/* advance ma1 */
			ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
		} else {
			/* found a match; advance both */
			if (ma0->write || ma1->write) {
				if (conflict)
					printf(", ");
				/* the match is also a conflict */
				print_shm_conflict(cpu, m0, m1, ma0, ma1);
				conflict = true;
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
		check_stack_conflict(ma0, tid1, &conflict);
		ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
	}
	while (ma1 != NULL) {
		check_stack_conflict(ma1, tid0, &conflict);
		ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
	}

	printf("}\n");

	return conflict;
}
