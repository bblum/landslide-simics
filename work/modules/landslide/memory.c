/**
 * @file memory.c
 * @brief routines for tracking dynamic allocations and otherwise shared memory
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <simics/api.h>

#define MODULE_NAME "MEMORY"
#define MODULE_COLOUR COLOUR_DARK COLOUR_YELLOW

#include "common.h"
#include "compiler.h"
#include "found_a_bug.h"
#include "html.h"
#include "kernel_specifics.h"
#include "kspec.h"
#include "landslide.h"
#include "memory.h"
#include "messaging.h"
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

#ifdef ALLOW_REENTRANT_MALLOC_FREE
#define KERN_MALLOC_STATE(ls) (&((ls)->sched.cur_agent->kern_malloc_flags))
#define USER_MALLOC_STATE(ls) (&((ls)->sched.cur_agent->user_malloc_flags))
#else
#define KERN_MALLOC_STATE(ls) (&((ls)->kern_mem.flags))
#define USER_MALLOC_STATE(ls) (&((ls)->user_mem.flags))
#endif

#define MALLOC_STATE(ls, in_kernel) \
	((in_kernel) ? KERN_MALLOC_STATE(ls) : USER_MALLOC_STATE(ls))

void init_malloc_actions(struct malloc_actions *flags)
{
	flags->in_alloc = false;
	flags->in_realloc = false;
	flags->in_free = false;
	flags->in_page_alloc = false;
	flags->in_page_free = false;
	flags->alloc_request_size = 0x1badd00d;
	flags->palloc_request_size = 0x2badd00d;
}

static void mem_heap_init(struct mem_state *m)
{
	m->malloc_heap.rb_node = NULL;
	m->heap_size = 0;
	m->heap_next_id = 0;
	m->guest_init_done = false;
	m->in_mm_init = false;
	m->palloc_heap.rb_node = NULL;
#ifndef ALLOW_REENTRANT_MALLOC_FREE
	init_malloc_actions(&m->flags);
#endif
	m->cr3 = USER_CR3_WAITING_FOR_THUNDERBIRDS;
	m->cr3_tid = 0;
	m->user_mutex_size = 0;
	m->during_xchg = false;
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
	} else if (TID_IS_INIT(current_tid) || TID_IS_SHELL(current_tid) ||
		   TID_IS_IDLE(current_tid)) {
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

/* As above, but searches both the malloc and palloc heap (if it exists). */
static struct chunk *find_alloced_chunk(struct mem_state *m, unsigned int addr)
{
	struct chunk *c = find_containing_chunk(&m->malloc_heap, addr);
	if (c == NULL) {
		c = find_containing_chunk(&m->palloc_heap, addr);
		/* Pages used to back malloc are still illegal. */
		if (c != NULL && c->pages_reserved_for_malloc) {
			c = NULL;
		}
	}
	return c;
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
	if (nobe == NULL) {
		return;
	}

	struct chunk *c = rb_entry(nobe, struct chunk, nobe);

	print_heap(v, c->nobe.rb_left, false);
	printf(v, "[0x%x | %d]", c->base, c->len);
	if (!rightmost || c->nobe.rb_right != NULL) {
		printf(v, ", ");
	}
	print_heap(v, c->nobe.rb_right, rightmost);
}

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

/* html env may be null */
static void print_freed_chunk_info(struct chunk *c,
				   struct hax *before, struct hax *after,
				   struct fab_html_env *html_env)
{
	char allocated_msg[BUF_SIZE];
	char freed_msg[BUF_SIZE];
	unsigned int pos = 0;

	scnprintf(allocated_msg, BUF_SIZE, "Heap block [0x%x | %d] "
		  "was allocated at:", c->base, c->len);

	pos += scnprintf(freed_msg + pos, BUF_SIZE - pos,
			 "...and, between preemptions ");
	if (after == NULL) {
		pos += scnprintf(freed_msg + pos, BUF_SIZE - pos, "[root]");
	} else {
		pos += scnprintf(freed_msg + pos, BUF_SIZE - pos, "#%d/tid%d",
				 after->depth, after->chosen_thread);
	}
	pos += scnprintf(freed_msg + pos, BUF_SIZE - pos, " and ");
	if (before == NULL) {
		pos += scnprintf(freed_msg + pos, BUF_SIZE - pos, "[latest]");
	} else {
		pos += scnprintf(freed_msg + pos, BUF_SIZE - pos, "#%d/tid%d",
				 before->depth, before->chosen_thread);
	}
	pos += scnprintf(freed_msg + pos, BUF_SIZE - pos, ", freed at:");

	if (html_env == NULL) {
		lsprintf(BUG, "%s", allocated_msg);
		print_stack_trace(BUG, c->malloc_trace);
		printf(BUG, "\n");
		lsprintf(BUG, "%s", freed_msg);
		print_stack_trace(BUG, c->free_trace);
		printf(BUG, "\n");
	} else {
		HTML_PRINTF(html_env, "%s" HTML_NEWLINE, allocated_msg);
		HTML_PRINTF(html_env, "TID %d at:" HTML_NEWLINE, c->malloc_trace->tid);
		HTML_PRINT_STACK_TRACE(html_env, c->malloc_trace);
		HTML_PRINTF(html_env, HTML_NEWLINE HTML_NEWLINE);
		HTML_PRINTF(html_env, "%s" HTML_NEWLINE, freed_msg);
		HTML_PRINTF(html_env, "TID %d at:" HTML_NEWLINE, c->free_trace->tid);
		HTML_PRINT_STACK_TRACE(html_env, c->free_trace);
		HTML_PRINTF(html_env, HTML_NEWLINE);
	}
}

/******************************************************************************
 * heap state tracking
 ******************************************************************************/

#define K_STR(in_kernel) ((in_kernel) ? "kernel" : "userspace")

/* In the 4 following functions, intialize pointers to the appropriate heaps
 * and flags depending on which heap (kmalloc, kpalloc, umalloc) is used. */
#define INIT_PTRS(m, heap, init, alloc, free, reqsize)				\
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;	\
	MAYBE_UNUSED struct rb_root *heap =					\
		is_palloc ? &m->palloc_heap : &m->malloc_heap;			\
	MAYBE_UNUSED bool *init  = &m->in_mm_init; /* gross, but harmless. */	\
	MAYBE_UNUSED bool *alloc = is_palloc ?					\
		&(MALLOC_STATE(ls, in_kernel)->in_page_alloc) :			\
		&(MALLOC_STATE(ls, in_kernel)->in_alloc);			\
	MAYBE_UNUSED bool *free  = is_palloc ?					\
		&(MALLOC_STATE(ls, in_kernel)->in_page_free) :			\
		&(MALLOC_STATE(ls, in_kernel)->in_free);			\
	MAYBE_UNUSED unsigned int *reqsize = is_palloc ?			\
		&(MALLOC_STATE(ls, in_kernel)->palloc_request_size) :		\
		&(MALLOC_STATE(ls, in_kernel)->alloc_request_size);		\
	assert((in_kernel || !is_palloc) && "user can't have a palloc heap");

/* bad place == mal loc */
/* so... pal loc == a friendly place? */
static void mem_enter_bad_place(struct ls_state *ls, bool in_kernel, bool is_palloc, unsigned int size)
{
	INIT_PTRS(m, heap, in_init, in_alloc, in_free, request_size);

	assert(!*in_init);
	if (*in_alloc || *in_free) {
		FOUND_A_BUG(ls, "Malloc (in %s) reentered %s!", K_STR(in_kernel),
			    *in_alloc ? "Malloc" : "Free");
	}

	*in_alloc = true;
	*request_size = size;
}

static void mem_exit_bad_place(struct ls_state *ls, bool in_kernel, bool is_palloc, unsigned int base)
{
	INIT_PTRS(m, heap, in_init, in_alloc, in_free, request_size);

	assert(*in_alloc && "attempt to exit malloc without being in!");
	assert(!*in_free && "attempt to exit malloc while in free!");
	assert(!*in_init && "attempt to exit malloc while in init!");

	if (in_kernel != testing_userspace()) {
		lsprintf(DEV, "Malloc [0x%x | %d]\n", base, *request_size);
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
		chunk->len = *request_size;
		chunk->id = m->heap_next_id;
		chunk->malloc_trace = stack_trace(ls);
		chunk->free_trace = NULL;
		/* In pintos, malloc() uses palloc() to back the arenas. We want
		 * to check for UAFs in both types of allocations, so specially
		 * flag palloced pages that should still UAF if there's an
		 * access not also part of a malloc()ed block inside.
		 * In pebbles, is_palloc will never be true. */
		chunk->pages_reserved_for_malloc = is_palloc &&
			within_function_st(chunk->malloc_trace, GUEST_LMM_ALLOC_ENTER,
					   GUEST_LMM_ALLOC_EXIT);

		m->heap_size += *request_size;
		assert(m->heap_next_id != INT_MAX && "need a wider type");
		m->heap_next_id++;
		insert_chunk(heap, chunk, false);
	}

	*in_alloc = false;
}

static void mem_enter_free(struct ls_state *ls, bool in_kernel, bool is_palloc, unsigned int base)
{
	INIT_PTRS(m, heap, in_init, in_alloc, in_free, request_size);

	struct chunk *chunk;

	assert(!*in_init);
	if (*in_alloc || *in_free) {
		FOUND_A_BUG(ls, "Free (in %s) reentered %s!", K_STR(in_kernel),
			    *in_alloc ? "Malloc" : "Free");
	}

	chunk = remove_chunk(heap, base);

	if (base == 0) {
		assert(chunk == NULL);
		lsprintf(INFO, "Free() NULL (in %s); ok, I guess...\n",
			 K_STR(in_kernel));
	} else if (chunk == NULL) {
		struct hax *before;
		struct hax *after;
		chunk = find_freed_chunk(ls, base, in_kernel, &before, &after);
		if (chunk != NULL) {
			print_freed_chunk_info(chunk, before, after, NULL);
			char buf[BUF_SIZE];
			int len = scnprintf(buf, BUF_SIZE, "DOUBLE FREE (in %s)"
					    " of 0x%x!", K_STR(in_kernel), base);
			FOUND_A_BUG_HTML_INFO(ls, buf, len, html_env,
				print_freed_chunk_info(chunk, before,
						       after, html_env);
			);
		} else {
			FOUND_A_BUG(ls, "Attempted to free (in %s) 0x%x, which was "
				    "never malloced!", K_STR(in_kernel), base);
		}
	} else if (chunk->base != base) {
		FOUND_A_BUG(ls, "Attempted to free 0x%x (in %s), which was not "
			    "malloced, but contained within another malloced "
			    "block: [0x%x | %d]", base,
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

	*in_free = true;
}

static void mem_exit_free(struct ls_state *ls, bool in_kernel, bool is_palloc)
{
	INIT_PTRS(m, heap, in_init, in_alloc, in_free, request_size);

	assert(*in_free && "attempt to exit free without being in!");
	assert(!*in_alloc && "attempt to exit free while in malloc!");
	assert(!*in_init && "attempt to exit free while in init!");
	*in_free = false;
}
#undef INIT_PTRS
#undef K_STR

void mem_update(struct ls_state *ls)
{
	/* Dynamic memory allocation tracking */
	unsigned int size;
	unsigned int base;
	unsigned int _orig_base;

	/* Only start tracking allocations after kernel_main is entered - the
	 * multiboot code that runs before kernel_main may confuse us. */
	if (!ls->kern_mem.guest_init_done) {
		if (kern_kernel_main(ls->eip)) {
			ls->kern_mem.guest_init_done = true;
		} else {
			return;
		}
	} else if (ls->sched.cur_agent->action.lmm_init) {
		return;
	}

	if (KERNEL_MEMORY(ls->eip)) {
		/* Normal malloc */
		if (kern_lmm_alloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, true, false, size);
		} else if (kern_lmm_alloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, true, false, base);
		} else if (kern_lmm_free_entering(ls->cpu0, ls->eip, &base)) {
			mem_enter_free(ls, true, false, base);
		} else if (kern_lmm_free_exiting(ls->eip)) {
			mem_exit_free(ls, true, false);
		/* Pintos-only separate page allocator */
		} else if (kern_page_alloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, true, true, size);
		} else if (kern_page_alloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, true, true, base);
		} else if (kern_page_free_entering(ls->cpu0, ls->eip, &base)) {
			mem_enter_free(ls, true, true, base);
		} else if (kern_page_free_exiting(ls->eip)) {
			mem_exit_free(ls, true, true);
		/* Etc. */
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
			mem_enter_bad_place(ls, false, false, size);
		} else if (user_mm_malloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, false, false, base);
			/* was this malloc part of a userspace mutex_init call?
			 * here is where we discover the structure of student
			 * mutexes that have dynamically-allocated bits. */
			if (ls->sched.cur_agent->action.user_mutex_initing) {
				learn_malloced_mutex_structure(&ls->user_sync,
					ls->sched.cur_agent->user_mutex_initing_addr,
					base, USER_MALLOC_STATE(ls)->alloc_request_size);
			}
		} else if (user_mm_free_entering(ls->cpu0, ls->eip, &base)) {
			mem_enter_free(ls, false, false, base);
		} else if (user_mm_free_exiting(ls->eip)) {
			mem_exit_free(ls, false, false);
		} else if (user_mm_realloc_entering(ls->cpu0, ls->eip, &_orig_base, &size)) {
			assert(!USER_MALLOC_STATE(ls)->in_alloc);
			assert(!USER_MALLOC_STATE(ls)->in_free);
			assert(!USER_MALLOC_STATE(ls)->in_realloc);
			USER_MALLOC_STATE(ls)->in_realloc = true;
		} else if (user_mm_realloc_exiting(ls->cpu0, ls->eip, &base)) {
			assert(!USER_MALLOC_STATE(ls)->in_alloc);
			assert(!USER_MALLOC_STATE(ls)->in_free);
			assert(USER_MALLOC_STATE(ls)->in_realloc);
			USER_MALLOC_STATE(ls)->in_realloc = false;
		} else if (user_mm_init_entering(ls->eip)) {
			assert(!USER_MALLOC_STATE(ls)->in_alloc);
			assert(!USER_MALLOC_STATE(ls)->in_free);
			ls->user_mem.in_mm_init = true;
		} else if (user_mm_init_exiting(ls->eip)) {
			assert(ls->user_mem.in_mm_init);
			assert(!USER_MALLOC_STATE(ls)->in_alloc);
			assert(!USER_MALLOC_STATE(ls)->in_free);
			ls->user_mem.in_mm_init = false;
		}
	}
}

/******************************************************************************
 * recording shm accesses (per-instruction)
 ******************************************************************************/

static void merge_chunk_id_info(enum chunk_id_info *dst_any, unsigned int *dst_id,
				enum chunk_id_info  src_any, unsigned int  src_id)
{
	if (*dst_any == MULTIPLE_CHUNK_IDS || src_any == MULTIPLE_CHUNK_IDS) {
		*dst_any = MULTIPLE_CHUNK_IDS; /* top */
	} else if (*dst_any == HAS_CHUNK_ID && src_any == HAS_CHUNK_ID) {
		if (*dst_id != src_id) {
			*dst_any = MULTIPLE_CHUNK_IDS; /* promote */
		} /* otherwise, nothing to do */
	} else if (src_any == HAS_CHUNK_ID) {
		assert(*dst_id == NOT_IN_HEAP);
		*dst_any = src_any; /* absorb */
		*dst_id = src_id;
	} /* otherwise, nothing to do */
}

static bool was_freed_remalloced(struct mem_lockset *l0, struct mem_lockset *l1)
{
	return l0->any_chunk_ids == HAS_CHUNK_ID &&
		l1->any_chunk_ids == HAS_CHUNK_ID &&
		l0->chunk_id != l1->chunk_id;
}

/* Actually looking for data races cannot happen until we know the
 * happens-before relationship to previous transitions, in save.c. */
static void add_lockset_to_shm(struct ls_state *ls, struct mem_access *ma,
			       struct chunk *c, bool write, bool in_kernel)
{
	struct lockset *current_locks =
		in_kernel ? &ls->sched.cur_agent->kern_locks_held :
		            &ls->sched.cur_agent->user_locks_held;
	struct mem_lockset *l_old;
	unsigned int current_syscall = ls->sched.cur_agent->most_recent_syscall;
	unsigned int called_from     = ls->sched.cur_agent->last_call;

	bool need_add = true;
	bool remove_prev = false;

	bool during_init    = INITING_SOMETHING(ls->sched.cur_agent);
	bool during_destroy = DESTROYING_SOMETHING(ls->sched.cur_agent);
	assert(!(during_init && during_destroy));

	bool interrupce = interrupts_enabled(ls->cpu0);

	enum chunk_id_info any_cids = c == NULL ? NOT_IN_HEAP : HAS_CHUNK_ID;
	unsigned int cid = c == NULL ? 0x15410de0u : c->id;

	/* Many memory accesses are repeated under identical circumstances (same
	 * line of code, same locks held, etc). This loop attempts not to create
	 * a new mem_lockset for each repeat access, instead coalescing them. */
	Q_FOREACH(l_old, &ma->locksets, nobe) {
		if (remove_prev) {
			struct mem_lockset *l_prev = l_old->nobe.prev;
			Q_REMOVE(&ma->locksets, l_prev, nobe);
			/* union ITS old chunk id info into OUR current one */
			merge_chunk_id_info(&any_cids, &cid, l_prev->any_chunk_ids,
					    l_prev->chunk_id);
			lockset_free(&l_prev->locks_held);
			MM_FREE(l_prev);
			remove_prev = false;
		}

		/* ensure init/destroy status matches */
		if (l_old->during_init != during_init ||
		    l_old->during_destroy != during_destroy) {
			/* it may be possible to establish a subset type of
			 * relation between these, and mush them together. */
			continue;
		}

		/* ensure most recent syscall matches */
		if (l_old->most_recent_syscall != current_syscall) {
			continue;
		}

		/* FIXME: is needed? is it too expensive to not merge these? */
		if (l_old->last_call != called_from) {
			continue;
		}

		/* ensure eip matches */
		if (l_old->eip != ls->eip) {
			continue;
		}

		/* ensure not merging into differently-interruptible access */
		if (l_old->interrupce_enabled != interrupce) {
			continue;
		}

		enum lockset_cmp_result r =
			lockset_compare(current_locks, &l_old->locks_held);
		if (r == LOCKSETS_SUPSET && write && !l_old->write) {
			/* e.g. current = L1 + L2; past = L2... BUT, current
			 * access is a write. while the old one with fewer locks
			 * was a write, so the accesses can't be merged. */
			continue;
		} else if (r == LOCKSETS_EQ || r == LOCKSETS_SUPSET) {
			/* if supset, e.g. current == L1 + L2; past == L2; then
			 * if current access is a write, the old one is too. so
			 * we already have a better lockset than the current one
			 * for finding data races on this access, so no need to
			 * add current in addition. */
			need_add = false;
			/* union OUR chunk id info into ITS old one */
			merge_chunk_id_info(&l_old->any_chunk_ids,
					    &l_old->chunk_id, any_cids, cid);
			/* in case of equal locksets and old being a read */
			l_old->write = l_old->write || write;
			break;
		} else if (r == LOCKSETS_SUBSET && (write || !l_old->write)) {
			/* e.g. current = L1; past = L1 + L2. in this case,
			 * current lockset is a strict upgrade over old (UNLESS
			 * old is a write and current isn't), so replace old. */
			remove_prev = true;
		} else {
			/* accesses cannot be merged in either direction. either
			 * LOCKSETS_DIFF, or current subset old but old was a
			 * write while current is only a read. */
			continue;
		}
	}

	if (remove_prev) {
		assert(need_add);
		l_old = Q_GET_TAIL(&ma->locksets);
		Q_REMOVE(&ma->locksets, l_old, nobe);
		lockset_free(&l_old->locks_held);
		MM_FREE(l_old);
	}

	if (need_add) {
		struct mem_lockset *l_new = MM_XMALLOC(1, struct mem_lockset);
		l_new->eip = ls->eip;
		l_new->write = write;
		l_new->during_init = during_init;
		l_new->during_destroy = during_destroy;
		l_new->interrupce_enabled = interrupce;
		l_new->last_call = called_from;
		l_new->most_recent_syscall = current_syscall;
		l_new->any_chunk_ids = any_cids;
		l_new->chunk_id = cid;
		lockset_clone(&l_new->locks_held, current_locks);
		Q_INSERT_FRONT(&ma->locksets, l_new, nobe);
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
			ma->any_writes = ma->any_writes || write;
			add_lockset_to_shm(ls, ma, c, write, in_kernel);
			return;
		}
	}

	/* doesn't exist; create a new one */
	ma = MM_XMALLOC(1, struct mem_access);
	ma->addr       = addr;
	ma->any_writes = write;
	ma->count      = 1;
	ma->conflict   = false;
	ma->other_tid  = 0;
	Q_INIT_HEAD(&ma->locksets);
	add_lockset_to_shm(ls, ma, c, write, in_kernel);

	rb_link_node(&ma->nobe, parent, p);
	rb_insert_color(&ma->nobe, &m->shm);
}

static void use_after_free(struct ls_state *ls, unsigned int addr,
			   bool write, bool in_kernel)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;

#ifndef PINTOS_KERNEL
	/* Students, if you are reading this, and you are interested in
	 * checking your libautostack implementation for use-after-free
	 * bugs, delete this check. This is a VERY COMMON bug. */
	if (testing_userspace() && KERNEL_MEMORY(ls->eip) &&
	    ls->sched.cur_agent->most_recent_syscall == SWEXN_INT) {
		return;
	}
#endif

	// TODO: do something analogous to a wrong_panic() assert here
	lsprintf(BUG, "Malloc() heap contents: {");
	print_heap(BUG, m->malloc_heap.rb_node, true);
	printf(BUG, "}\n");
	if (m->palloc_heap.rb_node != NULL) {
		lsprintf(BUG, "Palloc() heap contents: {");
		print_heap(BUG, m->palloc_heap.rb_node, true);
		printf(BUG, "}\n");
	}

	/* Find the chunk and print stack traces for it */
	struct hax *before;
	struct hax *after;
	struct chunk *c = find_freed_chunk(ls, addr, in_kernel, &before, &after);

	if (c == NULL) {
		lsprintf(BUG, "0x%x was never allocated...\n", addr);
		FOUND_A_BUG(ls, "INVALID HEAP ACCESS (never allocated) - "
			    "%s 0x%.8x at eip 0x%.8x",
			    write ? "write to" : "read from", addr,
			    (int)GET_CPU_ATTR(ls->cpu0, eip));
	} else {
		print_freed_chunk_info(c, before, after, NULL);
		char buf[BUF_SIZE];
		int len = scnprintf(buf, BUF_SIZE, "USE AFTER FREE - "
				    "%s 0x%.8x at eip 0x%.8x",
				    write ? "write to" : "read from", addr,
				    (unsigned int)GET_CPU_ATTR(ls->cpu0, eip));
		FOUND_A_BUG_HTML_INFO(ls, buf, len, html_env,
			print_freed_chunk_info(c, before, after, html_env);
		);
	}
}

/* some system calls, despite making no accesses to user memory, can still
 * affect another user thread's behaviour. essentially they allow user code to
 * use kernel memory as a communication backchannel. hence, if we are doing a
 * user-mode test, we still need to record those kernel memory accesses. */
#define SYSCALL_IS_USER_BACKCHANNEL(num)				\
	((num) == DESCHEDULE_INT || (num) == MAKE_RUNNABLE_INT ||	\
	 (num) == SET_STATUS_INT)

void mem_check_shared_access(struct ls_state *ls, unsigned int phys_addr,
			     unsigned int virt_addr, bool write)
{
	struct mem_state *m;
	bool in_kernel;
	unsigned int addr;

#ifndef PINTOS_KERNEL
	if (KERNEL_MEMORY(phys_addr) && virt_addr != 0) {
		/* non-page-table-read access in kernel mem. */
		assert(phys_addr == virt_addr && "kernel memory not direct-mapped??");
	}
#endif

	if (!ls->sched.guest_init_done) {
		return;
	}

	bool is_vm_user_copy = testing_userspace() &&
		ls->sched.cur_agent->action.vm_user_copy &&
		check_user_address_space(ls);

	/* Check for an xchg of a same value, to avoid too-liberally unblocking
	 * another xchg-blocked thread when this thread is blocked too. */
	bool xchg_wont_modify_the_data = false;
	if (opcodes_are_atomic_swap(ls->instruction_text)) {
		unsigned int val = READ_MEMORY(ls->cpu0, virt_addr);
		if (write) {
			/* all atomix ought read something before writing */
			assert(ls->user_mem.during_xchg);
			xchg_wont_modify_the_data =
				ls->user_mem.last_xchg_read == val;
		} else {
			ls->user_mem.during_xchg = true;
			ls->user_mem.last_xchg_read = val;
		}
	}

	/* Determine which heap - kernel or user - to reason about.
	 * Note: Need to case on eip's value, not addr's, since the
	 * kernel may access user memory for e.g. page tables. */
	if (KERNEL_MEMORY(ls->eip) && !is_vm_user_copy) {
		/* KERNEL SPACE */
		in_kernel = true;
#ifdef PINTOS_KERNEL
		/* Pintos is not direct-mapped. FIXME, this is a bit sloppy. */
		addr = virt_addr == 0 ? phys_addr : virt_addr;
#else
		/* Pebbles is. */
		addr = phys_addr;
#endif

		/* Certain components of the kernel have a free pass, such as
		 * the scheduler */
		if (kern_in_scheduler(ls->cpu0, ls->eip) ||
		    kern_access_in_scheduler(addr) ||
		    ls->sched.cur_agent->action.handling_timer || /* a hack */
		    ls->sched.cur_agent->action.context_switch) {
			return;
		}

		/* maintain invariant required in save.c (shimsham shm) that the
		 * shm heap for the space we're not testing stays empty. */
		if (testing_userspace()) {
			int syscall = ls->sched.cur_agent->most_recent_syscall;
			if (SYSCALL_IS_USER_BACKCHANNEL(syscall) &&
			    check_user_address_space(ls)) {
				/* record this access in the user's mem state
				 * XXX HACK: while this causes it to show up in
				 * conflicts as desired, it will also show up in
				 * DRs, which we must filter out later. */
				in_kernel = false;
				if (write && !xchg_wont_modify_the_data) {
					check_unblock_yield_loop(ls, addr);
				}
			} else {
				/* ignore access altogether. */
				return;
			}
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
			if (write && !xchg_wont_modify_the_data) {
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
		/* Current tid being the last h->chosen_thread indicates that
		 * we're still in the thread from the last PP. If the tid is
		 * still different from that, it's some 3rd unrelated thread. */
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
	} else if (ls->sched.voluntary_resched_stack != NULL &&
		   // XXX: can't rely on voluntary_resched_tid, see #178.
		   ls->sched.cur_agent->tid != ls->save.next_tid) {
		/* Uh oh, somehow we ran a different thread than the one chosen
		 * at the last PP. This can happen in Pintos when interrupts
		 * never get enabled between two yields (in the semaphores),
		 * but if interrupts are on, we definitely want to be noisy. */
		if (!TID_IS_IDLE(ls->sched.cur_agent->tid) &&
		    ls->save.next_tid != -1 && interrupts_enabled(ls->cpu0)) {
			lsprintf(DEV, COLOUR_BOLD COLOUR_YELLOW
				 "WARNING: ignoring shm by wrong thread(?) "
				 "(chosen TID %d; current TID %d) to 0x%x @ ",
				 ls->save.next_tid, ls->sched.cur_agent->tid, addr);
			print_eip(DEV, ls->eip);
			printf(DEV, "\n");
		}
		return;
	} else {
		/* Not a special "access belongs to someone else" situation. */
		if (in_kernel) {
			m = &ls->kern_mem;
		} else {
			m = &ls->user_mem;
		}
	}

	struct malloc_actions *flags = MALLOC_STATE(ls, in_kernel);

	/* the allocator has a free pass to its own accesses */
	if (m->in_mm_init || flags->in_alloc || flags->in_free ||
	    flags->in_realloc || flags->in_page_alloc || flags->in_page_free) {
		return;
	}

	/* userspace malloc wrappers have a free pass in terms of DPOR
	 * conflicts, but we still want to check them for use-after-free. */
	bool do_add_shm = !IN_USER_MALLOC_WRAPPERS(ls->sched.cur_agent);

	if ((in_kernel && kern_address_in_heap(addr)) ||
	    (!in_kernel && user_address_in_heap(addr))) {
		struct chunk *c = find_alloced_chunk(m, addr);
		if (c == NULL) {
			use_after_free(ls, addr, write, KERNEL_MEMORY(addr));
		} else if (do_add_shm) {
			add_shm(ls, m, c, addr, write, in_kernel);
		}
	} else if ((in_kernel && kern_address_global(addr)) ||
		   (!in_kernel /* && user_address_global(addr) */
		    && do_add_shm)) {
		/* Record shm accesses for user threads even on their own
		 * stacks, to deal with potential WISE IDEA yield loops. */
		add_shm(ls, m, NULL, addr, write, in_kernel);
	}
}

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
		if (c0 == NULL) {
			c0 = c1; /* default to "later" state if both exist */
		}
		/* If this happens, c0 and c1 were both not null and also got
		 * reallocated in-between. TODO: have better printing code.
		 * assert(c1 == NULL ||
		 *        (c0->base == c1->base && c0->len == c1->len));
		 */
		print_heap_address(buf, BUF_SIZE, ma0->addr, c0->base, c0->len);
	}
	printf(v, "[%s %c%d/%c%d]", buf, ma0->any_writes ? 'w' : 'r',
	       ma0->count, ma1->any_writes ? 'w' : 'r', ma1->count);
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
			if (*conflicts > 0) {
				printf(DEV, ", ");
			}
			struct mem_lockset *l = Q_GET_HEAD(&ma->locksets);
			assert(l != NULL);
			printf(DEV, "[tid%d stack %c%d 0x%x]", other_tid,
			       ma->any_writes ? 'w' : 'r', ma->count, l->eip);
		}
		ma->conflict = true;
		(*conflicts)++;
	}
}

static void check_freed_conflict(struct mem_access *ma0, struct mem_state *m1,
				 unsigned int other_tid, unsigned int *conflicts)
{
	// FIXME: Unimplemented for the palloc heap. What are the consequences?
	struct chunk *c = find_containing_chunk(&m1->freed, ma0->addr);

	if (c != NULL) {
		char buf[BUF_SIZE];
		print_heap_address(buf, BUF_SIZE, ma0->addr, c->base, c->len);

		if (*conflicts < MAX_CONFLICTS) {
			if (*conflicts > 0) {
				printf(DEV, ", ");
			}
			printf(DEV, "[%s %c%d (tid%d freed)]", buf,
			       ma0->any_writes ? 'w' : 'r', ma0->count, other_tid);
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
	/* a heuristic for distinguishing, among 'suspected' data races, those
	 * that are extra-suspicious by virtue of (a) the later access happening
	 * during a specified foo_destroy() function or (b) the earlier access
	 * happening during a specified foo_init() function.
	 *
	 * while normally 'suspected' data races are still possible to reorder
	 * (see issue #61 for explanation), this pattern allows us to suppress
	 * a lot of false positives that always-looking-at-suspecteds otherwise
	 * would identify.
	 *
	 * nb. l0 is the lockset for the later access; l1 for the earlier. */
	bool too_suspicious = l0->during_destroy || l1->during_init;

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
	printf(v, "]%s and \n", l0->interrupce_enabled ? "" : " (cli'd)");

	lsprintf(v, "%s", colour);
	printf(v, "#%d/tid%d at ", h1->depth, h1->chosen_thread);
	print_eip(v, l1->eip);
	printf(v, " [locks: ");
	lockset_print(v, &l1->locks_held);
	printf(v, "]%s\n", l1->interrupce_enabled ? "" : " (cli'd)");

	lsprintf(DEV, "Num data races suspected: %d; confirmed: %d\n",
		 m->data_races_suspected, m->data_races_confirmed);
	if (!confirmed && too_suspicious) {
		if (l0->during_destroy) {
			lsprintf(DEV, "Note: Suspected false positive due to "
				 "later access during a destroy().\n");
		}
		if (l1->during_init) {
			lsprintf(DEV, "Note: Suspected false positive due to "
				 "earlier access during a init().\n");
		}
	}
#endif
#endif
	/* Figure out if this data race was "deterministic"; i.e., it was found
	 * without any artificial preemptions. IOW, if we needed to preempt to
	 * get this DR report to begin with, it would have been a false NEGATIVE
	 * for a single-pass DR detector. (Note that if we already started with
	 * other DR PPs in this state space, we assume all DRs are *not*
	 * deterministic; if they were, they would have been found in the subset
	 * state space that generated the other DR to begin with.) */
	static const unsigned int data_race_info[][4] = DATA_RACE_INFO;
	bool deterministic = ARRAY_SIZE(data_race_info) == 0 &&
		ls->save.total_jumps == 0;

	/* Report to master process. If unconfirmed, it only helps to set a PP
	 * on the earlier one, so we don't send the later of suspected pairs. */
//#define DR_FALSE_NEGATIVE_EXPERIMENT
#ifdef DR_FALSE_NEGATIVE_EXPERIMENT
	STATIC_ASSERT(EXPLORE_BACKWARDS == 0 && "Hey, no fair!");
	/* a 1-pass dr analysis would report this on 1st branch w/o "waiting to
	 * reorder it"; it's not fair to count as a potential false negative */
	if (true) {
#else
	if (confirmed) {
#endif
		message_data_race(&ls->mess, l0->eip, h0->chosen_thread,
			l0->last_call, l0->most_recent_syscall, confirmed,
			deterministic);
	}
#ifdef DR_FALSE_NEGATIVE_EXPERIMENT
	if (true) {
#else
	if (confirmed || !too_suspicious) {
#endif
		message_data_race(&ls->mess, l1->eip, h1->chosen_thread,
			l1->last_call, l1->most_recent_syscall, confirmed,
			deterministic);
	}
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

static void check_enable_speculative_pp(struct hax *h, unsigned int eip)
{
	if (h != NULL && h->data_race_eip != -1 && h->data_race_eip == eip) {
		if (h->is_preemption_point) {
			lsprintf(DEV, "data race PP #%d/tid%d was already "
				 "enabled\n", h->depth, h->chosen_thread);
		} else {
			lsprintf(DEV, "data race enables PP #%d/tid%d\n",
				 h->depth, h->chosen_thread);
			h->is_preemption_point = true;
		}
	}
}

static void check_locksets(struct ls_state *ls, struct hax *h0, struct hax *h1,
			   struct mem_access *ma0, struct mem_access *ma1,
			   struct chunk *c0, struct chunk *c1, bool in_kernel)
{
	struct mem_state *m = in_kernel ? &ls->kern_mem : &ls->user_mem;
	struct mem_lockset *l0;
	struct mem_lockset *l1;

	assert(ma0->addr == ma1->addr);

	if (testing_userspace() && KERNEL_MEMORY(ma0->addr)) {
		/* Kernel memory access was recorded because it came from a
		 * "user thread communication backchannel" syscall. Since we
		 * aren't tracking kernel mutexes, suppress false positives. */
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
			if ((l0->write || l1->write)
			    && !lockset_intersect(&l0->locks_held, &l1->locks_held)
#ifndef DR_FALSE_NEGATIVE_EXPERIMENT
			    // TODO: If this is the case, message quicksand anyway,
			    // but tell it never explore this, just report it at the end.
			    //  for a FALSE POSITIVE experiment.
			    && !was_freed_remalloced(l0, l1)
#endif
			    && (l0->interrupce_enabled || l1->interrupce_enabled)
			    && !ignore_dr_function(l0->eip)
			    && !ignore_dr_function(l1->eip)) {
				/* Data race. Have we seen it reordered? */
				bool confirmed = check_data_race(m, l0->eip, l1->eip);
				print_data_race(ls, h0, h1, ma0, ma1, c0, c1,
						l0, l1, in_kernel, confirmed);
				/* Whether or not we saw it reordered, check if
				 * it enables a speculative DR save point. */
				check_enable_speculative_pp(h1->parent, l1->eip);
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
			if (ma0->any_writes || ma1->any_writes) {
				struct chunk *c0 = find_alloced_chunk(m0, ma0->addr);
				struct chunk *c1 = find_alloced_chunk(m1, ma1->addr);
				if (conflicts < MAX_CONFLICTS) {
					if (conflicts > 0) {
						printf(DEV, ", ");
					}
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

	if (conflicts > MAX_CONFLICTS) {
		printf(DEV, ", and %d more", conflicts - MAX_CONFLICTS);
	}
	printf(DEV, "}\n");

	return conflicts > 0;
}
