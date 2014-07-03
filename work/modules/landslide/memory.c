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
#include "stack.h"
#include "symtable.h"
#include "tree.h"
#include "user_specifics.h"
#include "user_sync.h"
#include "x86.h"

/* Values for user process cr3 state machine. We would like to assert that no
 * test case run during testing_userspace() uses multiple cr3 values, as that
 * would screw up our memory access tracking. But, we have to wait until after
 * the test case's process goes through exec() before recording its ultimate cr3
 * value. See ignore-user-access for details. */
#define USER_CR3_WAITING_FOR_THUNDERBIRDS 0
#define USER_CR3_WAITING_FOR_EXEC 1
#define USER_CR3_EXEC_HAPPENED 2

static void mem_heap_init(struct mem_state *m)
{
	m->heap.rb_node = NULL;
	m->heap_size = 0;
	m->heap_next_id = 0;
	m->guest_init_done = false;
	m->in_mm_init = false;
	m->in_alloc = false;
	m->in_free = false;
	m->cr3 = USER_CR3_WAITING_FOR_THUNDERBIRDS;
	m->cr3_tid = 0;
	m->user_mutex_size = 0;
	m->shm.rb_node = NULL;
	m->freed.rb_node = NULL;
	m->data_races.rb_node = NULL;
	m->data_races_suspected = 0;
	m->data_races_confirmed = 0;
}

void mem_init(struct ls_state *ls)
{
	mem_heap_init(&ls->kern_mem);
	mem_heap_init(&ls->user_mem);
}

/* The user mem heap tracking can only work for a single address space. We want
 * to pay attention to the userspace program under test, not the shell or init
 * or idle or anything like that. Figure out what that process's cr3 is. */
static bool ignore_user_access(struct ls_state *ls)
{
	unsigned int current_tid = ls->sched.cur_agent->tid;
	unsigned int cr3 = GET_CPU_ATTR(ls->cpu0, cr3);;

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
	} else if (ls->user_mem.cr3 == USER_CR3_WAITING_FOR_THUNDERBIRDS) {
		ls->user_mem.cr3 = USER_CR3_WAITING_FOR_EXEC;
		ls->user_mem.cr3_tid = current_tid;
		return true;
	} else if (ls->user_mem.cr3 == USER_CR3_WAITING_FOR_EXEC) {
		/* must wait for a trip through kernelspace; see below */
		return true;
	} else if (ls->user_mem.cr3 == USER_CR3_EXEC_HAPPENED) {
		/* recognized non-shell-non-idle-non-init user process has been
		 * through exec and back. hopefully its new cr3 is permanent. */
		assert(cr3 != USER_CR3_WAITING_FOR_EXEC);
		assert(cr3 != USER_CR3_EXEC_HAPPENED);
		ls->user_mem.cr3 = cr3;
		lsprintf(DEV, "Registered cr3 value 0x%x for userspace "
			 "tid %d.\n", cr3, current_tid);
		return false;
	} else if (ls->user_mem.cr3 != cr3) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Memory tracking for "
			 "more than 1 user address space is unsupported!\n");
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Already tracking for "
			 "cr3 0x%x, belonging to tid %d; current cr3 0x%x, "
			 "current tid %d\n", ls->user_mem.cr3,
			 ls->user_mem.cr3_tid, cr3, current_tid);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "If you're trying to "
			 "run vanish_vanish, make sure TESTING_USERSPACE=0.\n");
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Otherwise, make sure "
			 "your test case doesn't fork().\n" COLOUR_DEFAULT);
		assert(0);
		return false;
	} else {
		return false;
	}
}

/* Are we currently in the address space associated with the test program? */
bool check_user_address_space(struct ls_state *ls)
{
	return ls->user_mem.cr3 != USER_CR3_WAITING_FOR_THUNDERBIRDS &&
		ls->user_mem.cr3 != USER_CR3_WAITING_FOR_EXEC &&
		ls->user_mem.cr3 != USER_CR3_EXEC_HAPPENED &&
		ls->user_mem.cr3 == GET_CPU_ATTR(ls->cpu0, cr3);
}

/******************************************************************************
 * Heap helpers
 ******************************************************************************/

/* Returns NULL (and sets chunk) if a chunk containing addr (i.e., base <= addr
 * && addr < base + len) already exists.
 * Does not set parent if the heap is empty. */
static struct rb_node **find_insert_location(struct rb_root *root, unsigned int addr,
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
static struct chunk *find_containing_chunk(struct rb_root *root, unsigned int addr)
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

static struct chunk *remove_chunk(struct rb_root *root, unsigned int addr)
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
static struct chunk *find_freed_chunk(struct ls_state *ls, unsigned int addr,
				      bool in_kernel,
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
		scnprintf(after_buf, BUF_SIZE, "<root>");
	} else {
		scnprintf(after_buf, BUF_SIZE, "#%d/tid%d", after->depth,
			  after->chosen_thread);
	}
	if (before == NULL) {
		scnprintf(before_buf, BUF_SIZE, "<current>");
	} else {
		scnprintf(before_buf, BUF_SIZE, "#%d/tid%d", before->depth,
			  before->chosen_thread);
	}

	// TODO: when print stack trace can be made to use printf instead of lsprintf
	lsprintf(BUG, "[0x%x | %d] was allocated by ", c->base, c->len);
	print_stack_trace(BUG, c->malloc_trace);
	printf(BUG, "\n");
	lsprintf(BUG, "...and, between preemptions %s and %s, freed by ",
		 after_buf, before_buf);
	print_stack_trace(BUG, c->free_trace);
	printf(BUG, "\n");
}

/******************************************************************************
 * heap state tracking
 ******************************************************************************/

#define K_STR(in_kernel) ((in_kernel) ? "kernel" : "userspace")

/* bad place == mal loc */
static void mem_enter_bad_place(struct ls_state *ls, bool in_kernel, unsigned int size)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	assert(!m->in_mm_init);
	if (m->in_alloc || m->in_free) {
		FOUND_A_BUG(ls, "Malloc (in %s) reentered %s!", K_STR(in_kernel),
			    m->in_alloc ? "Malloc" : "Free");
	}

	m->in_alloc = true;
	m->alloc_request_size = size;
}

static void mem_exit_bad_place(struct ls_state *ls, bool in_kernel, unsigned int base)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	assert(m->in_alloc && "attempt to exit malloc without being in!");
	assert(!m->in_free && "attempt to exit malloc while in free!");
	assert(!m->in_mm_init && "attempt to exit malloc while in init!");

	if (in_kernel != testing_userspace()) {
		lsprintf(DEV, "Malloc [0x%x | %d]\n", base, m->alloc_request_size);
	}

	if (in_kernel) {
		assert(KERNEL_MEMORY(base));
	} else {
		assert(base == 0 || USER_MEMORY(base));
	}

	if (base == 0) {
		lsprintf(INFO, "%s seems to be out of memory.\n", K_STR(in_kernel));
	} else {
		struct chunk *chunk = MM_XMALLOC(1, struct chunk);
		chunk->base = base;
		chunk->len = m->alloc_request_size;
		chunk->id = m->heap_next_id;
		chunk->malloc_trace = stack_trace(ls);
		chunk->free_trace = NULL;

		m->heap_size += m->alloc_request_size;
		assert(m->heap_next_id != INT_MAX && "need a wider type");
		m->heap_next_id++;
		insert_chunk(&m->heap, chunk, false);
	}

	m->in_alloc = false;
}

static void mem_enter_free(struct ls_state *ls, bool in_kernel, unsigned int base)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	struct chunk *chunk;

	assert(!m->in_mm_init);
	if (m->in_alloc || m->in_free) {
		FOUND_A_BUG(ls, "Free (in %s) reentered %s!", K_STR(in_kernel),
			    m->in_alloc ? "Malloc" : "Free");
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
			print_freed_chunk_info(chunk, before, after);
			FOUND_A_BUG(ls, "DOUBLE FREE (in %s) of 0x%x!",
				    K_STR(in_kernel), base);
		} else {
			FOUND_A_BUG(ls, "Attempted to free (in %s) 0x%x, which was "
				    "never alloc'ed!", K_STR(in_kernel), base);
		}
	} else if (chunk->base != base) {
		FOUND_A_BUG(ls, "Attempted to free 0x%x (in %s), "
			    "contained within [0x%x | %d]", base,
			    K_STR(in_kernel), chunk->base, chunk->len);
	} else if (in_kernel != testing_userspace()) {
		lsprintf(DEV, "Free() chunk 0x%x, in %s\n", base, K_STR(in_kernel));
	}

	if (chunk != NULL) {
		m->heap_size -= chunk->len;
		assert(chunk->free_trace == NULL);
		chunk->free_trace = stack_trace(ls);
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

void mem_update(struct ls_state *ls)
{
	/* Dynamic memory allocation tracking */
	unsigned int size;
	unsigned int base;

	/* Only start tracking allocations after kernel_main is entered - the
	 * multiboot code that runs before kernel_main may confuse us. */
	if (!ls->kern_mem.guest_init_done) {
		if (kern_kernel_main(ls->eip)) {
			ls->kern_mem.guest_init_done = true;
		} else {
			return;
		}
	} else if (ls->sched.cur_agent->action.lmm_remove_free) {
		return;
	}

	if (KERNEL_MEMORY(ls->eip)) {
		if (kern_lmm_alloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, true, size);
		} else if (kern_lmm_alloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, true, base);
		} else if (kern_lmm_free_entering(ls->cpu0, ls->eip, &base, &size)) {
			mem_enter_free(ls, true, base);
		} else if (kern_lmm_free_exiting(ls->eip)) {
			mem_exit_free(ls, true);
		} else if (testing_userspace() && kern_exec_enter(ls->eip)) {
			/* Update user process cr3 state machine.
			 * See ignore-user-access above and below. */
			if (ls->user_mem.cr3_tid == ls->sched.cur_agent->tid &&
			    ls->user_mem.cr3 == USER_CR3_WAITING_FOR_EXEC) {
				ls->user_mem.cr3 = USER_CR3_EXEC_HAPPENED;
			}
		}
	} else {
		if (ignore_user_access(ls)) {
			return;
		} else if (user_mm_malloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, false, size);
		} else if (user_mm_malloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, false, base);
			/* was this malloc part of a userspace mutex_init call?
			 * here is where we discover the structure of student
			 * mutexes that have dynamically-allocated bits. */
			if (ls->sched.cur_agent->action.user_mutex_initing) {
				learn_malloced_mutex_structure(&ls->user_sync,
					ls->sched.cur_agent->user_mutex_initing_addr,
					base, ls->user_mem.alloc_request_size);
			}
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

/******************************************************************************
 * recording shm accesses (per-instruction)
 ******************************************************************************/

/* Actually looking for data races cannot happen until we know the
 * happens-before relationship to previous transitions, in save.c. */
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
		l->eip = ls->eip;
		lockset_clone(&l->locks_held, l0);
		Q_INSERT_FRONT(&ma->locksets, l, nobe);
	}
}

#define MEM_ENTRY(rb) \
	((rb) == NULL ? NULL : rb_entry(rb, struct mem_access, nobe))

static void add_shm(struct ls_state *ls, struct mem_state *m, struct chunk *c,
		    unsigned int addr, bool write, bool in_kernel)
{
	struct rb_node **p = &m->shm.rb_node;
	struct rb_node *parent = NULL;
	struct mem_access *ma;

	while (*p != NULL) {
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
	ma->count     = 1;
	ma->conflict  = false;
	ma->other_tid = 0;
	Q_INIT_HEAD(&ma->locksets);
	add_lockset_to_shm(ls, ma, in_kernel);

	rb_link_node(&ma->nobe, parent, p);
	rb_insert_color(&ma->nobe, &m->shm);
}

static void use_after_free(struct ls_state *ls, unsigned int addr,
			   bool write, bool in_kernel)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

	// TODO: do something analogous to a wrong_panic() assert here
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

	FOUND_A_BUG(ls, "USE AFTER FREE - %s 0x%.8x at eip"
		    " 0x%.8x", write ? "write to" : "read from", addr,
		    (int)GET_CPU_ATTR(ls->cpu0, eip));
}

void mem_check_shared_access(struct ls_state *ls, unsigned int phys_addr,
			     unsigned int virt_addr, bool write)
{
	struct mem_state *m;
	bool in_kernel;
	unsigned int addr;

	if (KERNEL_MEMORY(phys_addr) && virt_addr != 0) {
		/* non-page-table-read access in kernel mem. */
		assert(phys_addr == virt_addr && "kernel memory not direct-mapped??");
	}

	if (!ls->sched.guest_init_done) {
		return;
	}

	/* Determine which heap - kernel or user - to reason about.
	 * Note: Need to case on eip's value, not addr's, since the
	 * kernel may access user memory for e.g. page tables. */
	if (KERNEL_MEMORY(ls->eip)) {
		/* KERNEL SPACE */
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
	} else {
		/* USER SPACE */
		if (KERNEL_MEMORY(phys_addr)) {
			/* The 'int' instruction in userspace will cause a bunch
			 * of accesses to the kernel stack. Ignore them. */
			return;
		} else if (ignore_user_access(ls)) {
			/* Ignore accesses from userspace programs such as shell,
			 * idle, or shell-post-fork-pre-exec. Or ignore accesses
			 * if we're in a kernel test (esp. vanish_vanish!). */
			return;
		} else if (virt_addr == 0) {
			/* Read from page table. */
			assert(!write && "userspace write to page table??");
			return;
		} else {
			in_kernel = false;
			/* Use VA, not PA, for obviously important reasons. */
			addr = virt_addr;
			if (write) {
				check_user_mutex_access(ls, addr);
				check_unblock_yield_loop(ls, addr);
			}
		}
	}

	/* figure out which mem state to use. almost always, use the "current"
	 * mem. however, right after a decision point, that instruction's
	 * associated memory access (e.g. push %ebp) won't occur until after
	 * save-setjmp updates the tree, in which case we need to use the
	 * saved oldmem for the "last" transition. */
	if (ls->sched.schedule_in_flight != NULL) {
		assert(ls->sched.schedule_in_flight->tid != ls->sched.cur_agent->tid);
		assert(ls->save.current != NULL);
		if (ls->sched.cur_agent->tid != ls->save.current->chosen_thread) {
			/* Don't record this access at all if this is just an
			 * "intermediate" thread during a schedule-in-flight,
			 * i.e., this thread wasn't the one that ran last. */
			return;
		} else if (in_kernel) {
			m = ls->save.current->old_kern_mem;
		} else {
			m = ls->save.current->old_user_mem;
		}
	} else {
		/* Not a special "access belongs to someone else" situation. */
		if (in_kernel) {
			m = &ls->kern_mem;
		} else {
			m = &ls->user_mem;
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
			use_after_free(ls, addr, write, KERNEL_MEMORY(addr));
		} else {
			add_shm(ls, m, c, addr, write, in_kernel);
		}
	} else if ((in_kernel && kern_address_global(addr)) ||
		   (!in_kernel /* && user_address_global(addr) */)) {
		/* Record shm accesses for user threads even on their own
		 * stacks, to deal with potential WISE IDEA yield loops. */
		add_shm(ls, m, NULL, addr, write, in_kernel);
	}
}

// XXX: Shm trees a re sorted with signed comparison. Fix that.
bool shm_contains_addr(struct mem_state *m, unsigned int addr)
{
	struct mem_access *ma = MEM_ENTRY(m->shm.rb_node);

	while (ma != NULL) {
		if (addr == ma->addr) {
			return true;
		} else if (addr < ma->addr) {
			ma = MEM_ENTRY(ma->nobe.rb_left);
		} else {
			ma = MEM_ENTRY(ma->nobe.rb_right);
		}
	}
	return false;
}

/******************************************************************************
 * checking shm conflicts (per-preemption-point)
 ******************************************************************************/

#define print_heap_address(buf, size, addr, base, len) \
	scnprintf(buf, size, "0x%x in [0x%x | %d]", addr, base, len)

static void print_shm_conflict(verbosity v, struct mem_state *m0, struct mem_state *m1,
			       struct mem_access *ma0, struct mem_access *ma1,
			       struct chunk *c0, struct chunk *c1)
{
	char buf[BUF_SIZE];
	bool in_kernel = KERNEL_MEMORY(ma0->addr);

	assert(ma0->addr == ma1->addr);

	if (c0 == NULL && c1 == NULL) {
		if ((in_kernel && kern_address_in_heap(ma0->addr)) ||
		    (!in_kernel && user_address_in_heap(ma0->addr))) {
			/* This could happen if both transitions did a heap
			 * access, then did free() on the corresponding chunk
			 * before the next choice point. TODO: free() might
			 * itself be a good place to set choice points... */
			scnprintf(buf, BUF_SIZE, "heap0x%.8x", ma0->addr);
		} else if (!in_kernel && !user_address_global(ma0->addr)) {
			/* Userspace stack access. */
			scnprintf(buf, BUF_SIZE, "stack0x%.8x", ma0->addr);
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
		print_heap_address(buf, BUF_SIZE, ma0->addr, c0->base, c0->len);
	}
	printf(v, "[%s %c%d/%c%d]", buf, ma0->write ? 'w' : 'r', ma0->count,
	       ma1->write ? 'w' : 'r', ma1->count);
}

#define MAX_CONFLICTS 10

static void check_stack_conflict(struct mem_access *ma, unsigned int other_tid,
				 unsigned int *conflicts)
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
			struct mem_lockset *l = Q_GET_HEAD(&ma->locksets);
			assert(l != NULL);
			printf(DEV, "[tid%d stack %c%d 0x%x]", other_tid,
			       ma->write ? 'w' : 'r', ma->count, l->eip);
		}
		ma->conflict = true;
		(*conflicts)++;
	}
}

static void check_freed_conflict(struct mem_access *ma0, struct mem_state *m1,
				 unsigned int other_tid, unsigned int *conflicts)
{
	struct chunk *c = find_containing_chunk(&m1->freed, ma0->addr);

	if (c != NULL) {
		char buf[BUF_SIZE];
		print_heap_address(buf, BUF_SIZE, ma0->addr, c->base, c->len);

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

static void print_data_race(struct ls_state *ls, struct hax *h0, struct hax *h1,
			    struct mem_access *ma0, struct mem_access *ma1,
			    struct chunk *c0, struct chunk *c1,
			    struct mem_lockset *l0, struct mem_lockset *l1,
			    bool in_kernel, bool confirmed)
{
#ifdef PRINT_DATA_RACES
#if PRINT_DATA_RACES != 0
	struct mem_state *m  = in_kernel ? &ls->kern_mem    : &ls->user_mem;
	struct mem_state *m0 = in_kernel ? h0->old_kern_mem : h0->old_user_mem;
	struct mem_state *m1 = in_kernel ? h1->old_kern_mem : h1->old_user_mem;

	const char *colour =
		confirmed ? COLOUR_BOLD COLOUR_RED : COLOUR_BOLD COLOUR_YELLOW;
	verbosity v = confirmed ? CHOICE : DEV;

	lsprintf(v, "%sData race: ", colour);
	print_shm_conflict(v, m0, m1, ma0, ma1, c0, c1);
	printf(v, " between:\n");

	lsprintf(v, "%s", colour);
	printf(v, "#%d/tid%d at ", h0->depth, h0->chosen_thread);
	print_eip(v, l0->eip);
	printf(v, " [locks: ");
	lockset_print(v, &l0->locks_held);
	printf(v, "] and \n");

	lsprintf(v, "%s", colour);
	printf(v, "#%d/tid%d at ", h1->depth, h1->chosen_thread);
	print_eip(v, l1->eip);
	printf(v, " [locks: ");
	lockset_print(v, &l1->locks_held);
	printf(v, "]\n");

	lsprintf(DEV, "Num data races suspected: %d; confirmed: %d\n",
		 m->data_races_suspected, m->data_races_confirmed);
#endif
#endif
}

/* checks for both orderings of eips in a suspected data race, occurring across
 * multiple branches of the state space, before confirming the possibility of a
 * data race. see comment above struct data_race in memory.h for reasoning. */
static bool check_data_race(struct mem_state *m, unsigned int eip0, unsigned int eip1)
{
	struct rb_node **p = &m->data_races.rb_node;
	struct rb_node *parent = NULL;
	struct data_race *dr;

	/* lower eip is first, regardless of order */
	bool eip0_first = eip0 < eip1;
	unsigned int first_eip = eip0_first ? eip0 : eip1;
	unsigned int other_eip = eip0_first ? eip1 : eip0;

	while (*p != NULL) {
		parent = *p;
		dr = rb_entry(parent, struct data_race, nobe);
		assert(dr->first_before_other || dr->other_before_first);

		/* lexicographic ordering of eips */
		if (first_eip < dr->first_eip ||
		    (first_eip == dr->first_eip && other_eip < dr->other_eip)) {
			p = &(*p)->rb_left;
		} else if (first_eip > dr->first_eip ||
			   (first_eip == dr->first_eip && other_eip > dr->other_eip)) {
			p = &(*p)->rb_right;
		} else {
			assert(first_eip == dr->first_eip);
			assert(other_eip == dr->other_eip);

			/* found existing entry for eip pair; check eip order */
			if (eip0_first && dr->other_before_first) {
				if (!dr->first_before_other) {
					dr->first_before_other = true;
					m->data_races_confirmed++;
				}
				return true;
			} else if (!eip0_first && dr->first_before_other) {
				if (!dr->other_before_first) {
					dr->other_before_first = true;
					m->data_races_confirmed++;
				}
				return true;
			} else {
				/* second order not observed yet */
				return false;
			}
		}
	}

	/* this eip pair has never raced before; create a new entry */
	dr = MM_XMALLOC(1, struct data_race);
	dr->first_eip          = first_eip;
	dr->other_eip          = other_eip;
	dr->first_before_other = eip0_first;
	dr->other_before_first = !eip0_first;

	rb_link_node(&dr->nobe, parent, p);
	rb_insert_color(&dr->nobe, &m->data_races);

	m->data_races_suspected++;
	return false;
}

static void check_locksets(struct ls_state *ls, struct hax *h0, struct hax *h1,
			   struct mem_access *ma0, struct mem_access *ma1,
			   struct chunk *c0, struct chunk *c1, bool in_kernel)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;
	struct mem_lockset *l0;
	struct mem_lockset *l1;

	if (c0 != NULL && c1 != NULL && c0->id != c1->id) {
		/* The apparent data race was actually in a part of the heap
		 * that was freed and re-malloced in between. */
		return;
	}

	/* Note since we're just identifying *suspected* data races here, we
	 * need to pay attention to each distinct eip pair, rather than just
	 * calling it a day after the first matching eip pair. (IOW, suppose
	 * ma0's eips are [A,B], ma1's eips are [C,D], and the race (A,C) ends
	 * up being nonreorderable, while (B,D) is reorderable: in this case
	 * stopping after (A,C) would report no confirmed data races at all.) */
	Q_FOREACH(l0, &ma0->locksets, nobe) {
		Q_FOREACH(l1, &ma1->locksets, nobe) {
			/* Are there any 2 locksets without a lock in common? */
			if (!lockset_intersect(&l0->locks_held, &l1->locks_held)) {
				bool confirmed = check_data_race(m, l0->eip, l1->eip);
				print_data_race(ls, h0, h1, ma0, ma1, c0, c1,
						l0, l1, in_kernel, confirmed);
			}
		}
	}
}

/* Compute the intersection of two transitions' shm accesses */
bool mem_shm_intersect(struct ls_state *ls, struct hax *h0, struct hax *h1,
		       bool in_kernel)
{
	struct mem_state *m0 = in_kernel ? h0->old_kern_mem : h0->old_user_mem;
	struct mem_state *m1 = in_kernel ? h1->old_kern_mem : h1->old_user_mem;
	unsigned int tid0 = h0->chosen_thread;
	unsigned int tid1 = h1->chosen_thread;

	struct mem_access *ma0 = MEM_ENTRY(rb_first(&m0->shm));
	struct mem_access *ma1 = MEM_ENTRY(rb_first(&m1->shm));
	unsigned int conflicts = 0;

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
			check_freed_conflict(ma0, m1, tid1, &conflicts);
			/* advance ma0 */
			ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
		} else if (ma0->addr > ma1->addr) {
			check_stack_conflict(ma1, tid0, &conflicts);
			check_freed_conflict(ma1, m0, tid0, &conflicts);
			/* advance ma1 */
			ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
		} else {
			/* found a match; advance both */
			if (ma0->write || ma1->write) {
				struct chunk *c0 =
					find_containing_chunk(&m0->heap, ma0->addr);
				struct chunk *c1 =
					find_containing_chunk(&m1->heap, ma1->addr);
				if (conflicts < MAX_CONFLICTS) {
					if (conflicts > 0)
						printf(DEV, ", ");
					/* the match is also a conflict */
					print_shm_conflict(DEV, m0, m1,
							   ma0, ma1, c0, c1);
				}
				conflicts++;
				ma0->conflict = true;
				ma1->conflict = true;
				// FIXME: make this not interleave horribly with conflicts
				check_locksets(ls, h0, h1, ma0, ma1, c0, c1, in_kernel);
			}
			ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
			ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
		}
	}

	/* even if one transition runs out of recorded accesses, we still need
	 * to check the other one's remaining accesses for the one's stack. */
	while (ma0 != NULL) {
		check_stack_conflict(ma0, tid1, &conflicts);
		check_freed_conflict(ma0, m1, tid1, &conflicts);
		ma0 = MEM_ENTRY(rb_next(&ma0->nobe));
	}
	while (ma1 != NULL) {
		check_stack_conflict(ma1, tid0, &conflicts);
		check_freed_conflict(ma1, m0, tid0, &conflicts);
		ma1 = MEM_ENTRY(rb_next(&ma1->nobe));
	}

	if (conflicts > MAX_CONFLICTS)
		printf(DEV, ", and %d more", conflicts - MAX_CONFLICTS);
	printf(DEV, "}\n");

	return conflicts > 0;
}
