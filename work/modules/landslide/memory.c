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
#include "user_specifics.h"
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

static struct chunk *remove_chunk(struct rb_root *root, int addr)
{
	struct chunk *target = NULL;
	struct rb_node **p = find_insert_location(root, addr, &target);

	if (p == NULL) {
		assert(target != NULL);
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

#define BUF_SIZE 256

/* Attempt to find a freed chunk among all transitions */
static struct chunk *find_freed_chunk(struct ls_state *ls, int addr, bool in_kernel,
				      struct hax **before, struct hax **after)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

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
			m = in_kernel ? (*before)->old_kern_mem :
			                (*before)->old_user_mem;
		}
	} while (*before != NULL);

	return NULL;
}

static void print_freed_chunk_info(struct chunk *c, struct hax *before, struct hax *after)
{
	char before_buf[BUF_SIZE];
	char after_buf[BUF_SIZE];

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
}

/******************************************************************************
 * shm helpers
 ******************************************************************************/

// Actually looking for data races cannot happen until we know the
// happens-before relationship to previous transitions, in save.c.
static void add_lockset_to_shm(struct ls_state *ls, struct mem_access *ma,
			       bool in_kernel)
{
	struct lockset *l0 = in_kernel ? &ls->sched.cur_agent->kern_locks_held :
	                                 &ls->sched.cur_agent->user_locks_held;
	struct mem_lockset *l;

	bool need_add = true;
	bool remove_prev = false;

	Q_FOREACH(l, &ma->locksets, nobe) {
		if (remove_prev) {
			struct mem_lockset *l_old = l->nobe.prev;
			Q_REMOVE(&ma->locksets, l_old, nobe);
			lockset_free(&l_old->locks_held);
			MM_FREE(l_old);
		}

		enum lockset_cmp_result r = lockset_compare(l0, &l->locks_held);
		if (r == LOCKSETS_EQ || r == LOCKSETS_SUPSET) {
			// if l subset l0, then we already have a better lockset
			// than l0 for finding data races on this access, so no
			// need to add l0 in addition.
			need_add = false;
			break;
		} else {
			// if subset, then l0 would be a strict upgrade over l,
			// in terms of finding data races, so we can remove l.
			remove_prev = (r == LOCKSETS_SUBSET);
		}
	}

	if (remove_prev) {
		l = Q_GET_TAIL(&ma->locksets);
		Q_REMOVE(&ma->locksets, l, nobe);
	}

	if (need_add) {
		l = MM_XMALLOC(1, struct mem_lockset);
		lockset_clone(&l->locks_held, l0);
		Q_INSERT_FRONT(&ma->locksets, l, nobe);
	}
}

#define MEM_ENTRY(rb) \
	((rb) == NULL ? NULL : rb_entry(rb, struct mem_access, nobe))

static void add_shm(struct ls_state *ls, struct mem_state *m, struct chunk *c,
		    int addr, bool write, bool in_kernel)
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
			add_lockset_to_shm(ls, ma, in_kernel);
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
	Q_INIT_HEAD(&ma->locksets);
	add_lockset_to_shm(ls, ma, in_kernel);

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

static void mem_heap_init(struct mem_state *m)
{
	m->heap.rb_node = NULL;
	m->heap_size = 0;
	m->guest_init_done = false;
	m->in_mm_init = false;
	m->in_alloc = false;
	m->in_free = false;
	m->cr3 = 0;
	m->user_mutex_size = 0;
	m->shm.rb_node = NULL;
	m->freed.rb_node = NULL;
}

void mem_init(struct ls_state *ls)
{
	mem_heap_init(&ls->kern_mem);
	mem_heap_init(&ls->user_mem);
}

/* heap interface */

#define K_STR(in_kernel) ((in_kernel) ? "kernel" : "userspace")

/* bad place == mal loc */
static void mem_enter_bad_place(struct ls_state *ls, bool in_kernel, int size)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	assert(!m->in_mm_init);
	if (m->in_alloc || m->in_free) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "Malloc (in %s) reentered %s!\n",
			 K_STR(in_kernel), m->in_alloc ? "Malloc" : "Free");
		found_a_bug(ls);
	}

	m->in_alloc = true;
	m->alloc_request_size = size;
}

static void mem_exit_bad_place(struct ls_state *ls, bool in_kernel, int base)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	assert(m->in_alloc && "attempt to exit malloc without being in!");
	assert(!m->in_free && "attempt to exit malloc while in free!");
	assert(!m->in_mm_init && "attempt to exit malloc while in init!");

	if (in_kernel != testing_userspace()) {
		lsprintf(DEV, "Malloc [0x%x | %d]\n", base, m->alloc_request_size);
	}

	if (in_kernel) {
		assert(base < USER_MEM_START);
	} else {
		assert(base >= USER_MEM_START);
	}

	if (base == 0) {
		lsprintf(INFO, "%s seems to be out of memory.\n", K_STR(in_kernel));
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

static void mem_enter_free(struct ls_state *ls, bool in_kernel, int base)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	struct chunk *chunk;

	assert(!m->in_mm_init);
	if (m->in_alloc || m->in_free) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "Free (in %s) reentered %s!\n",
			 K_STR(in_kernel), m->in_alloc ? "Malloc" : "Free");
		found_a_bug(ls);
	}

	chunk = remove_chunk(&m->heap, base);

	if (base == 0) {
		assert(chunk == NULL);
		lsprintf(INFO, "Free() NULL (in %s); ok, I guess...\n", K_STR(in_kernel));
	} else if (chunk == NULL) {
		struct hax *before;
		struct hax *after;
		chunk = find_freed_chunk(ls, base, in_kernel, &before, &after);
		if (chunk != NULL) {
			lsprintf(BUG, COLOUR_BOLD COLOUR_RED "DOUBLE FREE (in %s) "
				 "of 0x%x!\n", K_STR(in_kernel), base);
			print_freed_chunk_info(chunk, before, after);
		} else {
			lsprintf(BUG, COLOUR_BOLD COLOUR_RED
				 "Attempted to free (in %s) 0x%x, which was "
				 "never alloc'ed!\n", K_STR(in_kernel), base);
		}
		found_a_bug(ls);
	} else if (chunk->base != base) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "Attempted to free 0x%x "
			 "(in %s), contained within [0x%x | %d]\n", base,
			 K_STR(in_kernel), chunk->base, chunk->len);
		found_a_bug(ls);
	} else if (in_kernel != testing_userspace()) {
		lsprintf(DEV, "Free() chunk 0x%x, in %s\n", base, K_STR(in_kernel));
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

static void mem_exit_free(struct ls_state *ls, bool in_kernel)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	assert(m->in_free && "attempt to exit free without being in!");
	assert(!m->in_alloc && "attempt to exit free while in malloc!");
	assert(!m->in_mm_init && "attempt to exit free while in init!");
	m->in_free = false;
}
#undef K_STR

/* The user mem heap tracking can only work for a single address space. We want
 * to pay attention to the userspace program under test, not the shell or init
 * or idle or anything like that. Figure out what that process's cr3 is. */
static bool ignore_user_access(struct ls_state *ls)
{
	int current_tid = ls->sched.cur_agent->tid;
	int cr3 = GET_CPU_ATTR(ls->cpu0, cr3);;

	if (!testing_userspace()) {
		/* Don't attempt to track user accesses for kernelspace tests.
		 * Tests like vanish_vanish require multiple user cr3s, which
		 * we don't support when tracking user accesses. When doing a
		 * userspace test, we need to do the below cr3 assertion, but
		 * when doing a kernel test we cannot, so instead we have to
		 * ignore all user accesses entirely. */
		return true;
	} else if (current_tid == kern_get_init_tid() ||
	    current_tid == kern_get_shell_tid() ||
	    (kern_has_idle() && current_tid == kern_get_idle_tid())) {
		return true;
	} else if (ls->user_mem.cr3 == 0) {
		ls->user_mem.cr3 = cr3;
		lsprintf(DEV, "Registered cr3 value 0x%x for userspace "
			 "tid %d.\n", cr3, current_tid);
		return false;
	} else if (ls->user_mem.cr3 != cr3) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Memory tracking for "
			 "more than 1 user address space is unsupported!\n");
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Already tracking for "
			 "cr3 0x%x; current cr3 0x%x; current tid %d\n",
			 ls->user_mem.cr3, cr3, current_tid);
		assert(0);
		return false;
	} else {
		return false;
	}
}

/* Some p2s may have open-coded mutex unlock actions (outside of mutex_*()
 * functions) to help with thread exiting. Detect these and unblock contenders. */
static void check_user_mutex_access(struct ls_state *ls, unsigned int addr)
{
	struct mem_state *m = &ls->user_mem;
	assert(addr >= USER_MEM_START);
	assert(m->cr3 != 0 && "attempt to check user mutex before cr3 is known");

	if (m->user_mutex_size == 0) {
		// Learning user mutex size relies on the user symtable being
		// registered. Delay this until one surely has been.
		int _lock_addr;
		if (user_mutex_init_entering(ls->cpu0, ls->eip, &_lock_addr)) {
			m->user_mutex_size = learn_user_mutex_size();
		} else {
			return;
		}
	}
	assert(m->user_mutex_size > 0);

	struct agent *a = ls->sched.cur_agent;
	if (a->action.user_mutex_locking || a->action.user_mutex_unlocking) {
		return;
	}

	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		unsigned int lock_addr = (unsigned int)a->user_blocked_on_addr;
		if (lock_addr != (unsigned int)(-1) &&
		    addr >= lock_addr && addr < lock_addr + m->user_mutex_size) {
			lsprintf(DEV, "Rogue write to %x, unblocks tid %d from %x\n",
				 addr, a->tid, lock_addr);
			a->user_blocked_on_addr = -1;
		}
	);
}

void mem_update(struct ls_state *ls)
{
	/* Dynamic memory allocation tracking */
	int size;
	int base;

	/* Only start tracking allocations after kernel_main is entered - the
	 * multiboot code that runs before kernel_main may confuse us. */
	if (!ls->kern_mem.guest_init_done) {
		if (kern_kernel_main(ls->eip)) {
			ls->kern_mem.guest_init_done = true;
		} else {
			return;
		}
	}

	if (within_function(ls->cpu0, ls->eip, GUEST_LMM_REMOVE_FREE_ENTER,
			    GUEST_LMM_REMOVE_FREE_EXIT)) {
		return;
	}

	if (ls->eip < USER_MEM_START) {
		if (kern_lmm_alloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, true, size);
		} else if (kern_lmm_alloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, true, base);
		} else if (kern_lmm_free_entering(ls->cpu0, ls->eip, &base, &size)) {
			mem_enter_free(ls, true, base);
		} else if (kern_lmm_free_exiting(ls->eip)) {
			mem_exit_free(ls, true);
		}
	} else {
		if (ignore_user_access(ls)) {
			return;
		} else if (user_mm_malloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, false, size);
		} else if (user_mm_malloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, false, base);
		} else if (user_mm_free_entering(ls->cpu0, ls->eip, &base)) {
			mem_enter_free(ls, false, base);
		} else if (user_mm_free_exiting(ls->eip)) {
			mem_exit_free(ls, false);
		} else if (user_mm_init_entering(ls->eip)) {
			assert(!ls->user_mem.in_alloc);
			assert(!ls->user_mem.in_free);
			ls->user_mem.in_mm_init = true;
		} else if (user_mm_init_exiting(ls->eip)) {
			assert(ls->user_mem.in_mm_init);
			assert(!ls->user_mem.in_alloc);
			assert(!ls->user_mem.in_free);
			ls->user_mem.in_mm_init = false;
		}
	}
}

/* shm interface */

static void use_after_free(struct ls_state *ls, int addr, bool write, bool in_kernel)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	// TODO: do something analogous to a wrong_panic() assert here
	lsprintf(BUG, COLOUR_BOLD COLOUR_RED "USE AFTER FREE - %s 0x%.8x at eip"
		 " 0x%.8x\n", write ? "write to" : "read from", addr,
		 (int)GET_CPU_ATTR(ls->cpu0, eip));
	lsprintf(BUG, "Heap contents: {");
	print_heap(BUG, m->heap.rb_node, true);
	printf(BUG, "}\n");

	/* Find the chunk and print stack traces for it */
	struct hax *before;
	struct hax *after;
	struct chunk *c = find_freed_chunk(ls, addr, in_kernel, &before, &after);

	if (c == NULL) {
		lsprintf(BUG, "0x%x was never allocated...\n", addr);
	} else {
		print_freed_chunk_info(c, before, after);
	}

	found_a_bug(ls);
}

void mem_check_shared_access(struct ls_state *ls, int phys_addr, int virt_addr,
			     bool write)
{
	struct mem_state *m;
	bool in_kernel;
	int addr;

	if (phys_addr < USER_MEM_START && virt_addr != 0) {
		/* non-page-table-read access in kernel mem. */
		assert(phys_addr == virt_addr && "kernel memory not direct-mapped??");
	}

	if (!ls->sched.guest_init_done) {
		return;
	}

	/* Determine which heap - kernel or user - to reason about.
	 * Note: Need to case on eip's value, not addr's, since the
	 * kernel may access user memory for e.g. page tables. */
	if (ls->eip < USER_MEM_START) {
		/* KERNEL SPACE */
		m = &ls->kern_mem;
		in_kernel = true;
		addr = phys_addr;

		/* Certain components of the kernel have a free pass, such as
		 * the scheduler - TODO: make this configurable */
		if (kern_in_scheduler(ls->cpu0, ls->eip) ||
		    kern_access_in_scheduler(addr) ||
		    ls->sched.cur_agent->action.handling_timer || /* XXX: a hack */
		    ls->sched.cur_agent->action.context_switch) {
			return;
		}

		/* maintain invariant required in save.c (shimsham shm) that the
		 * shm heap for the space we're not testing stays empty. */
		if (testing_userspace()) {
			return;
		}
#ifndef STUDENT_FRIENDLY
		/* ignore certain "probably innocent" accesses */
		if (kern_address_own_kstack(ls->cpu0, addr)) {
			return;
		}
#endif
	} else {
		/* USER SPACE */
		if (phys_addr < USER_MEM_START) {
			/* The 'int' instruction in userspace will cause a bunch
			 * of accesses to the kernel stack. Ignore them. */
			return;
		} else if (ignore_user_access(ls)) {
			/* Ignore accesses from userspace programs such as shell,
			 * idle, or shell-post-fork-pre-exec. */
			return;
		} else if (virt_addr == 0) {
			/* Read from page table. */
			assert(!write && "userspace write to page table??");
			return;
		} else {
			m = &ls->user_mem;
			in_kernel = false;
			/* Use VA, not PA, for obviously important reasons. */
			addr = virt_addr;
			check_user_mutex_access(ls, (unsigned int)addr);
		}
	}

	/* the allocator has a free pass to its own accesses */
	if (m->in_mm_init || m->in_alloc || m->in_free) {
		return;
	}

	if ((in_kernel && kern_address_in_heap(addr)) ||
	    (!in_kernel && user_address_in_heap(addr))) {
		struct chunk *c = find_containing_chunk(&m->heap, addr);
		if (c == NULL) {
			use_after_free(ls, addr, write, addr < USER_MEM_START);
		} else {
			add_shm(ls, m, c, addr, write, in_kernel);
		}
	} else if ((in_kernel && kern_address_global(addr)) ||
		   (!in_kernel && user_address_global(addr))) {
		add_shm(ls, m, NULL, addr, write, in_kernel);
	}
}

/* Remove the need for the student to implement kern_address_hint. */
#ifdef STUDENT_FRIENDLY
#define kern_address_hint(cpu, buf, size, addr, base, len) \
	snprintf(buf, size, "0x%.8x in [0x%x | %d]", addr, base, len)
#endif

static void print_shm_conflict(verbosity v, conf_object_t *cpu,
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
	printf(v, "[%s %c%d/%c%d]", buf, ma0->write ? 'w' : 'r', ma0->count,
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

static void check_data_race(conf_object_t *cpu,
			    struct mem_state *m0, struct mem_state *m1,
			    struct mem_access *ma0, struct mem_access *ma1)
{
	struct mem_lockset *l0;
	struct mem_lockset *l1;

	Q_FOREACH(l0, &ma0->locksets, nobe) {
		Q_FOREACH(l1, &ma1->locksets, nobe) {
			// To be safe, all pairs of locksets must have a lock in common.
			if (!lockset_intersect(&l0->locks_held, &l1->locks_held)) {
				char buf[BUF_SIZE];
				lsprintf(CHOICE, COLOUR_BOLD COLOUR_RED "Data race: ");
				print_shm_conflict(CHOICE, cpu, m0, m1, ma0, ma1);
				printf(CHOICE, " at:\n");
				symtable_lookup(buf, BUF_SIZE, ma0->eip);
				lsprintf(CHOICE, COLOUR_BOLD COLOUR_RED
					 "0x%x %s and \n", ma0->eip, buf);
				symtable_lookup(buf, BUF_SIZE, ma1->eip);
				lsprintf(CHOICE, COLOUR_BOLD COLOUR_RED
					 "0x%x %s\n", ma1->eip, buf);
				return;
			}
		}
	}
}

/* Compute the intersection of two transitions' shm accesses */
bool mem_shm_intersect(conf_object_t *cpu, struct hax *h0, struct hax *h1,
                       bool in_kernel)
{
	struct mem_state *m0 = in_kernel ? h0->old_kern_mem : h0->old_user_mem;
	struct mem_state *m1 = in_kernel ? h1->old_kern_mem : h1->old_user_mem;
	int tid0 = h0->chosen_thread;
	int tid1 = h1->chosen_thread;

	struct mem_access *ma0 = MEM_ENTRY(rb_first(&m0->shm));
	struct mem_access *ma1 = MEM_ENTRY(rb_first(&m1->shm));
	int conflicts = 0;

	assert(h0->depth > h1->depth);
	assert(!h0->happens_before[h1->depth]);
	assert(h0->chosen_thread != h1->chosen_thread);

	/* Should not even be called for the -space not being tested. */
	assert(in_kernel != testing_userspace());

	lsprintf(DEV, "Intersecting transition %d (TID %d) with %d (TID %d): {",
		 h0->depth, tid0, h1->depth, tid1);

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
					print_shm_conflict(DEV, cpu, m0, m1,
							   ma0, ma1);
				}
				conflicts++;
				ma0->conflict = true;
				ma1->conflict = true;
				// FIXME: make this not interleave horribly with conflicts
				check_data_race(cpu, m0, m1, ma0, ma1);
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
