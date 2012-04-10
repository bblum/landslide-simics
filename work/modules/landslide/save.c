/**
 * @file save.c
 * @brief choice tree tracking, incl. save/restore
 * @author Ben Blum
 */

#include <assert.h>
#include <string.h> /* for memcmp, strlen */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> /* for open */
#include <unistd.h> /* for write */

#include <simics/alloc.h>
#include <simics/api.h>

#define MODULE_NAME "SAVE"
#define MODULE_COLOUR COLOUR_MAGENTA

#include "arbiter.h"
#include "common.h"
#include "compiler.h"
#include "found_a_bug.h"
#include "landslide.h"
#include "memory.h"
#include "save.h"
#include "schedule.h"
#include "test.h"
#include "tree.h"
#include "x86.h"

/******************************************************************************
 * simics goo
 ******************************************************************************/

/* The bookmark name is this prefix concatenated with the hexadecimal address
 * of the choice struct (no "0x"). */
#define BOOKMARK_PREFIX "landslide"
#define BOOKMARK_SUFFIX_LEN ((int)(2*sizeof(unsigned long long))) /* for hex */
#define BOOKMARK_MAX_LEN (strlen(BOOKMARK_PREFIX) + 16)

#define CMD_PROLOGUE "stop"
#define CMD_BOOKMARK "set-bookmark"
#define CMD_DELETE   "delete-bookmark"
#define CMD_SKIPTO   "skip-to"
#define CMD_BUF_LEN 64
#define MAX_CMD_LEN (MAX(strlen(CMD_BOOKMARK), \
			 MAX(strlen(CMD_DELETE),strlen(CMD_SKIPTO))))

/* Running commands is done by use of SIM_run_alone. We write the command out to
 * a file, and pause simics's execution. Our wrapper will cause the command file
 * to get executed. (This is necessary because simics refuses to run "skip-to"
 * from execution context.) */
struct cmd_packet {
	const char *file;
	const char *cmd;
	unsigned long long label;
};

static void run_command_cb(lang_void *addr)
{
	struct cmd_packet *p = (struct cmd_packet *)addr;
	char buf[CMD_BUF_LEN];
	int ret;
	int fd = open(p->file, O_CREAT | O_WRONLY | O_APPEND,
		      S_IRUSR | S_IWUSR);
	assert(fd != -1 && "failed open command file");

	/* Generate command */
	assert(CMD_BUF_LEN > strlen(p->cmd) + 1 + BOOKMARK_MAX_LEN);
	ret = snprintf(buf, CMD_BUF_LEN, "%s " BOOKMARK_PREFIX "%.*llx\n",
		       p->cmd, BOOKMARK_SUFFIX_LEN, p->label);
	assert(ret > 0 && "failed snprintf");
	ret = write(fd, buf, ret);
	assert(ret > 0 && "failed write");

	if (buf[strlen(buf)-1] == '\n') {
		buf[strlen(buf)-1] = '\0';
	}
	lsprintf(INFO, "Using file '%s' for cmd '%s'\n", p->file, buf);

	/* Clean-up */
	ret = close(fd);
	assert(ret == 0 && "failed close");

	MM_FREE(p);
}

static void run_command(const char *file, const char *cmd, struct hax *h)
{
	struct cmd_packet *p = MM_XMALLOC(1, struct cmd_packet);

	p->cmd   = cmd;
	p->label = (unsigned long long)h;
	p->file  = file;

	SIM_break_simulation(NULL);
	SIM_run_alone(run_command_cb, (lang_void *)p);
}

/******************************************************************************
 * helpers
 ******************************************************************************/

static struct agent *copy_agent(struct agent *a_src)
{
	struct agent *a_dest = MM_XMALLOC(1, struct agent);
	assert(a_src != NULL && "cannot copy null agent");

	a_dest->tid                    = a_src->tid;
	a_dest->action.handling_timer  = a_src->action.handling_timer;
	a_dest->action.context_switch  = a_src->action.context_switch;
	a_dest->action.cs_free_pass    = a_src->action.cs_free_pass;
	a_dest->action.forking         = a_src->action.forking;
	a_dest->action.sleeping        = a_src->action.sleeping;
	a_dest->action.vanishing       = a_src->action.vanishing;
	a_dest->action.readlining      = a_src->action.readlining;
	a_dest->action.just_forked     = a_src->action.just_forked;
	a_dest->action.mutex_locking   = a_src->action.mutex_locking;
	a_dest->action.mutex_unlocking = a_src->action.mutex_unlocking;
	a_dest->action.schedule_target = a_src->action.schedule_target;
	assert(memcmp(&a_dest->action, &a_src->action,
		      sizeof(a_dest->action)) == 0 &&
	       "Did you update agent->action without updating save.c?");

	a_dest->blocked_on      = NULL; /* Will be recomputed later if needed */
	a_dest->blocked_on_tid  = a_src->blocked_on_tid;
	a_dest->blocked_on_addr = a_src->blocked_on_addr;

	a_dest->do_explore = false;

	return a_dest;
}

/* Updates the cur_agent and schedule_in_flight pointers upon finding the
 * corresponding agence in the s_src. */
static void copy_sched_q(struct agent_q *q_dest, const struct agent_q *q_src,
			 struct sched_state *dest,
			 const struct sched_state *src)
{
	struct agent *a_src;

	assert(Q_GET_SIZE(q_dest) == 0);

	Q_FOREACH(a_src, q_src, nobe) {
		struct agent *a_dest = copy_agent(a_src);

		// XXX: Q_INSERT_TAIL causes an assert to trip. ???
		Q_INSERT_HEAD(q_dest, a_dest, nobe);
		if (src->cur_agent == a_src)
			dest->cur_agent = a_dest;
		if (src->last_agent != NULL && src->last_agent == a_src)
			dest->last_agent = a_dest;
		if (src->schedule_in_flight == a_src)
			dest->schedule_in_flight = a_dest;
	}
}
static void copy_sched(struct sched_state *dest, const struct sched_state *src)
{
	dest->cur_agent           = NULL;
	dest->last_agent          = NULL;
	dest->last_vanished_agent = NULL;
	dest->schedule_in_flight  = NULL;
	Q_INIT_HEAD(&dest->rq);
	Q_INIT_HEAD(&dest->dq);
	Q_INIT_HEAD(&dest->sq);
	copy_sched_q(&dest->rq, &src->rq, dest, src);
	copy_sched_q(&dest->dq, &src->dq, dest, src);
	copy_sched_q(&dest->sq, &src->sq, dest, src);
	assert((src->cur_agent == NULL || dest->cur_agent != NULL) &&
	       "copy_sched couldn't set cur_agent!");
	assert((src->schedule_in_flight == NULL ||
	        dest->schedule_in_flight != NULL) &&
	       "copy_sched couldn't set schedule_in_flight!");

	/* The last_vanished agent is not on any queues. */
	if (src->last_vanished_agent != NULL) {
		dest->last_vanished_agent =
			copy_agent(src->last_vanished_agent);
		if (src->last_agent == src->last_vanished_agent) {
			assert(dest->last_agent == NULL &&
			       "but last_agent was already found!");
			dest->last_agent = dest->last_vanished_agent;
		}
	} else {
		dest->last_vanished_agent = NULL;
	}

	/* Must be after the last_vanished copy in case it was the last_agent */
	assert((src->last_agent == NULL || dest->last_agent != NULL) &&
	       "copy_sched couldn't set last_agent!");

	dest->delayed_in_flight      = src->delayed_in_flight;
	dest->just_finished_reschedule = src->just_finished_reschedule;
	dest->current_extra_runnable = src->current_extra_runnable;
	dest->num_agents             = src->num_agents;
	dest->most_agents_ever       = src->most_agents_ever;
	dest->guest_init_done        = src->guest_init_done;
	dest->entering_timer         = src->entering_timer;
	dest->voluntary_resched_tid  = src->voluntary_resched_tid;
	dest->voluntary_resched_stack =
		(src->voluntary_resched_stack == NULL) ? NULL :
			MM_XSTRDUP(src->voluntary_resched_stack);
}
static void copy_test(struct test_state *dest, const struct test_state *src)
{
	dest->test_is_running  = src->test_is_running;
	dest->start_population = src->start_population;
	dest->start_heap_size  = src->start_heap_size;

	if (src->current_test == NULL) {
		dest->current_test = NULL;
	} else {
		dest->current_test = MM_XSTRDUP(src->current_test);
	}
}
static struct rb_node *dup_chunk(const struct rb_node *nobe,
				 const struct rb_node *parent)
{
	if (nobe == NULL)
		return NULL;

	struct chunk *src = rb_entry(nobe, struct chunk, nobe);
	struct chunk *dest = MM_XMALLOC(1, struct chunk);

	/* dup rb node contents */
	int colour_flag = src->nobe.rb_parent_color & 1;

	assert(((unsigned long)parent & 1) == 0);
	dest->nobe.rb_parent_color = (unsigned long)parent | colour_flag;
	dest->nobe.rb_right = dup_chunk(src->nobe.rb_right, &dest->nobe);
	dest->nobe.rb_left  = dup_chunk(src->nobe.rb_left, &dest->nobe);

	dest->base = src->base;
	dest->len = src->len;

	if (src->malloc_trace == NULL) {
		dest->malloc_trace = NULL;
	} else {
		dest->malloc_trace = MM_XSTRDUP(src->malloc_trace);
	}
	if (src->free_trace == NULL) {
		dest->free_trace = NULL;
	} else {
		dest->free_trace = MM_XSTRDUP(src->free_trace);
	}

	return &dest->nobe;
}
static void copy_mem(struct mem_state *dest, const struct mem_state *src)
{
	dest->in_alloc           = src->in_alloc;
	dest->in_free            = src->in_free;
	dest->alloc_request_size = src->alloc_request_size;
	dest->heap.rb_node       = dup_chunk(src->heap.rb_node, NULL);
	dest->heap_size          = src->heap_size;
}

/* To free copied state data structures. None of these free the arg pointer. */
static void free_sched_q(struct agent_q *q)
{
	while (Q_GET_SIZE(q) > 0) {
		struct agent *a = Q_GET_HEAD(q);
		Q_REMOVE(q, a, nobe);
		MM_FREE(a);
	}
}
static void free_sched(struct sched_state *s)
{
	free_sched_q(&s->rq);
	free_sched_q(&s->dq);
	free_sched_q(&s->sq);
}
static void free_test(const struct test_state *t)
{
	MM_FREE(t->current_test);
}

static void free_heap(struct rb_node *nobe)
{
	if (nobe == NULL)
		return;
	free_heap(nobe->rb_left);
	free_heap(nobe->rb_right);

	struct chunk *c = rb_entry(nobe, struct chunk, nobe);
	if (c->malloc_trace != NULL) MM_FREE(c->malloc_trace);
	if (c->free_trace   != NULL) MM_FREE(c->free_trace);
	MM_FREE(c);
}

static void free_shm(struct rb_node *nobe)
{
	if (nobe == NULL)
		return;
	free_shm(nobe->rb_left);
	free_shm(nobe->rb_right);
	MM_FREE(rb_entry(nobe, struct mem_access, nobe));
}

static void free_mem(struct mem_state *m)
{
	free_heap(m->heap.rb_node);
	m->heap.rb_node = NULL;
	free_shm(m->shm.rb_node);
	m->shm.rb_node = NULL;
	free_heap(m->freed.rb_node);
	m->freed.rb_node = NULL;
}

static void free_arbiter_choices(struct arbiter_state *a)
{
// TODO: deprecate one of these
#if 0
	while (Q_GET_SIZE(&a->choices) > 0) {
		struct choice *c = Q_GET_HEAD(&a->choices);
		Q_REMOVE(&a->choices, c, nobe);
		lsprintf(INFO, "discarding buffered arbiter choice %d\n", c->tid);
		MM_FREE(c);
	}
#else
	struct choice *c;
	Q_FOREACH(c, &a->choices, nobe) {
		lsprintf(INFO, "Preserving buffered arbiter choice %d\n", c->tid);
	}
#endif
}

static void free_hax(struct hax *h)
{
	free_sched(h->oldsched);
	MM_FREE(h->oldsched);
	free_test(h->oldtest);
	MM_FREE(h->oldtest);
	free_mem(h->oldmem);
	MM_FREE(h->oldmem);
	h->oldsched = NULL;
	h->oldtest = NULL;
	h->oldmem = NULL;
	MM_FREE(h->conflicts);
	MM_FREE(h->happens_before);
	h->conflicts = NULL;
	h->happens_before = NULL;
	MM_FREE(h->stack_trace);
}

/* Reverse that which is not glowing green. */
static void restore_ls(struct ls_state *ls, struct hax *h)
{
	lsprintf(BRANCH, "88 MPH: eip 0x%x -> 0x%x; "
		 "triggers %lu -> %lu (absolute %lu); last choice %d\n",
		 ls->eip, h->eip, ls->trigger_count, h->trigger_count,
		 ls->absolute_trigger_count, h->chosen_thread);

	ls->eip           = h->eip;
	ls->trigger_count = h->trigger_count;

	// TODO: can have "move" instead of "copy" for these
	free_sched(&ls->sched);
	copy_sched(&ls->sched, h->oldsched);
	free_test(&ls->test);
	copy_test(&ls->test, h->oldtest);
	free_mem(&ls->mem);
	copy_mem(&ls->mem, h->oldmem); /* note: leaves shm empty, as we want */
	free_arbiter_choices(&ls->arbiter);

	ls->just_jumped = true;
}

/******************************************************************************
 * Independence and happens-before computation
 ******************************************************************************/

static void shimsham_shm(conf_object_t *cpu, struct hax *h, struct mem_state *m)
{
	/* store shared memory accesses from this transition; reset to empty */
	h->oldmem->shm.rb_node = m->shm.rb_node;
	m->shm.rb_node = NULL;

	/* do the same for the list of freed chunks in this transition */
	h->oldmem->freed.rb_node = m->freed.rb_node;
	m->freed.rb_node = NULL;

	/* compute newly-completed transition's conflicts with previous ones */
	for (struct hax *old = h->parent; old != NULL; old = old->parent) {
		if (h->chosen_thread == old->chosen_thread) {
			lsprintf(INFO, "Same TID %d for #%d and #%d\n",
				 h->chosen_thread, h->depth, old->depth);
			continue;
		} else if (kern_has_idle() &&
			   (h->chosen_thread == kern_get_idle_tid() ||
			    old->chosen_thread == kern_get_idle_tid())) {
			/* Idle shouldn't have siblings, but just in case. */
			h->conflicts[old->depth] = true;
			continue;
		} else if (old->depth == 0) {
			/* Basically guaranteed. Suppress printing. */
			h->conflicts[0] = true;
			continue;
		}
		/* The haxes are independent if there was no intersection. */
		assert(old->depth >= 0 && old->depth < h->depth);
		h->conflicts[old->depth] =
			mem_shm_intersect(cpu, h->oldmem, old->oldmem, h->depth,
					  h->chosen_thread, old->depth,
					  old->chosen_thread);
	}
}

static void inherit_happens_before(struct hax *h, struct hax *old)
{
	for (int i = 0; i < old->depth; i++) {
		h->happens_before[i] =
			h->happens_before[i] || old->happens_before[i];
	}
}

static bool enabled_by(struct hax *h, struct hax *old)
{
	if (old->parent == NULL) {
		lsprintf(INFO, "#%d/tid%d has no parent\n",
			 old->depth, old->chosen_thread);
		return true;
	} else {
		struct agent *a;
		lsprintf(INFO, "Searching for #%d/tid%d among siblings of "
			 "#%d/tid%d: ", h->depth, h->chosen_thread, old->depth,
			 old->chosen_thread);
		print_qs(INFO, old->parent->oldsched);
		FOR_EACH_RUNNABLE_AGENT(a, old->parent->oldsched,
			if (a->tid == h->chosen_thread && !BLOCKED(a)) {
				printf(INFO, "not enabled_by\n");
				return false;
			}
		);
		/* Transition A enables transition B if B was not a sibling of
		 * A; i.e., if before A was run B could not have been chosen. */
		printf(INFO, "yes enabled_by\n");
		return true;
	}
}

static void compute_happens_before(struct hax *h)
{
	int i;

	/* Between two transitions of a thread X, X_0 before X_1, while there
	 * may be many transitions Y that for which enabled_by(X_1, Y), only the
	 * earliest such Y (the one soonest after X_0) is the actual enabler. */
	struct hax *enabler = NULL;

	for (i = 0; i < h->depth; i++) {
		h->happens_before[i] = false;
	}

	for (struct hax *old = h->parent; old != NULL; old = old->parent) {
		assert(--i == old->depth); /* sanity check */
		assert(old->depth >= 0 && old->depth < h->depth);
		if (h->chosen_thread == old->chosen_thread) {
			h->happens_before[old->depth] = true;
			inherit_happens_before(h, old);
			/* Computing any further would be redundant, and would
			 * break the true-enabler finding alg. */
			break;
		} else if (enabled_by(h, old)) {
			enabler = old;
			h->happens_before[enabler->depth] = true;
		}
	}

	/* Take the happens-before set of the oldest enabler. */
	if (enabler != NULL) {
		h->happens_before[enabler->depth] = true;
		inherit_happens_before(h, enabler);
	}

	lsprintf(DEV, "Transitions { ");
	for (i = 0; i < h->depth; i++) {
		if (h->happens_before[i]) {
			printf(DEV, "#%d ", i);
		}
	}
	printf(DEV, "} happen-before #%d/tid%d.\n", h->depth, h->chosen_thread);
}

/******************************************************************************
 * interface
 ******************************************************************************/

void save_init(struct save_state *ss)
{
	ss->root = NULL;
	ss->current = NULL;
	ss->next_tid = -1;
	ss->total_choice_poince = 0;
	ss->total_choices = 0;
	ss->total_jumps = 0;
	ss->total_triggers = 0;
	ss->depth_total = 0;
}

void save_recover(struct save_state *ss, struct ls_state *ls, int new_tid)
{
	/* After a longjmp, we will be on exactly the node we jumped to, but
	 * there must be a special call to let us know what our new course is
	 * (see sched_recover). */
	assert(ls->just_jumped);
	ss->next_tid = new_tid;
	lsprintf(DEV, "explorer chose tid %d; ready for action\n", new_tid);
}

/* In the typical case, this signifies that we have reached a new decision
 * point. We:
 *  - Add a new choice node to signify this
 *  - The TID that was chosen to get here is stored in ss->new_tid; copy that
 *  - Create a list of possible TIDs to explore from this choice point
 *  - Store the new_tid for some subsequent choice point
 */
void save_setjmp(struct save_state *ss, struct ls_state *ls,
		 int new_tid, bool our_choice, bool end_of_test, bool voluntary)
{
	struct hax *h;

	lsprintf(INFO, "tid %d to eip 0x%x, where we %s tid %d\n", ss->next_tid,
		 ls->eip, our_choice ? "choose" : "follow", new_tid);

	/* Whether there should be a choice node in the tree is dependent on
	 * whether the current pending choice was our decision or not. The
	 * explorer's choice (!ours) will be in anticipation of a new node, but
	 * at that point we won't have the info to create the node until we go
	 * one step further. */
	if (our_choice) {
		h = MM_XMALLOC(1, struct hax);

		h->eip           = ls->eip;
		h->trigger_count = ls->trigger_count;
		h->chosen_thread = ss->next_tid;

		/* Put the choice into the tree. */
		if (ss->root == NULL) {
			/* First/root choice. */
			assert(ss->current == NULL);
			assert(end_of_test || ss->next_tid == -1);

			h->parent = NULL;
			h->depth  = 0;
			ss->root  = h;
		} else {
			/* Subsequent choice. */
			assert(ss->current != NULL);
			assert(end_of_test || ss->next_tid != -1);

			// XXX: Q_INSERT_TAIL causes a sigsegv
			Q_INSERT_HEAD(&ss->current->children, h, sibling);
			h->parent = ss->current;
			h->depth = 1 + h->parent->depth;
		}

		Q_INIT_HEAD(&h->children);
		h->all_explored = end_of_test;

		if (voluntary) {
			assert(h->chosen_thread == -1 || h->chosen_thread ==
			       ls->sched.voluntary_resched_tid);
			assert(ls->sched.voluntary_resched_stack != NULL);
			h->stack_trace = ls->sched.voluntary_resched_stack;
			ls->sched.voluntary_resched_stack = NULL;
		} else {
			h->stack_trace = stack_trace(ls->cpu0, ls->eip,
						     ls->sched.cur_agent->tid);
		}
		lsprintf(DEV, "Stack: %s\n", h->stack_trace);

		ss->total_choice_poince++;
	} else {
		assert(ss->root != NULL);
		assert(ss->current != NULL);
		assert(end_of_test || ss->next_tid != -1);
		assert(!end_of_test);

		/* Find already-existing previous choice nobe */
		Q_SEARCH(h, &ss->current->children, sibling,
			 h->chosen_thread == ss->next_tid);
		assert(h != NULL && "!our_choice but chosen tid not found...");

		assert(h->eip == ls->eip);
		assert(h->trigger_count == ls->trigger_count);
		assert(h->oldsched == NULL);
		assert(h->oldtest == NULL);
		assert(h->oldmem == NULL);
		assert(!h->all_explored); /* exploration invariant */
	}

	h->oldsched = MM_XMALLOC(1, struct sched_state);
	copy_sched(h->oldsched, &ls->sched);

	h->oldtest = MM_XMALLOC(1, struct test_state);
	copy_test(h->oldtest, &ls->test);

	h->oldmem = MM_XMALLOC(1, struct mem_state);
	copy_mem(h->oldmem, &ls->mem);

	if (h->depth > 0) {
		h->conflicts      = MM_XMALLOC(h->depth, bool);
		h->happens_before = MM_XMALLOC(h->depth, bool);
		/* For progress sense. */
		ss->total_triggers +=
			ls->trigger_count - h->parent->trigger_count;
	} else {
		h->conflicts      = NULL;
		h->happens_before = NULL;
	}
	shimsham_shm(ls->cpu0, h, &ls->mem);
	compute_happens_before(h);

	ss->current  = h;
	ss->next_tid = new_tid;
	lsprintf(CHOICE, "After committing #%d/tid%d, next_tid %d\n",
		 h->depth, h->chosen_thread, ss->next_tid);

	run_command(ls->cmd_file, CMD_BOOKMARK, (lang_void *)h);
	ss->total_choices++;
}

void save_longjmp(struct save_state *ss, struct ls_state *ls, struct hax *h)
{
	struct hax *rabbit = ss->current;

	assert(ss->root != NULL && "Can't longjmp with no decision tree!");
	assert(ss->current != NULL);

	ss->depth_total += ss->current->depth;

	/* The caller is allowed to say NULL, which means jump to the root. */
	if (h == NULL)
		h = ss->root;

	/* Find the target choice point from among our ancestors. */
	while (ss->current != h) {
		/* This node will soon be in the future. Reclaim memory. */
		free_hax(ss->current);
		run_command(ls->cmd_file, CMD_DELETE, (lang_void *)ss->current);

		ss->current = ss->current->parent;

		/* We won't have simics bookmarks, or indeed some saved state,
		 * for non-ancestor choice points. */
		assert(ss->current != NULL && "Can't jump to a non-ancestor!");

		/* the tree must not have properties of Philip J. Fry */
		if (rabbit) rabbit = rabbit->parent;
		if (rabbit) rabbit = rabbit->parent;
		assert(rabbit != ss->current && "somehow, a cycle?!?");
	}

	PRINT_TREE_INFO(BRANCH, ls);

	restore_ls(ls, h);

	run_command(ls->cmd_file, CMD_SKIPTO, (lang_void *)h);
	ss->total_jumps++;
}
