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
#include "landslide.h"
#include "save.h"
#include "schedule.h"
#include "test.h"
#include "tree.h"

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
	int fd = open(p->file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
	assert(fd != -1 && "failed open command file");

	/* Generate command */
	assert(CMD_BUF_LEN > strlen(p->cmd) + 1 + BOOKMARK_MAX_LEN);
	ret = snprintf(buf, CMD_BUF_LEN, "%s " BOOKMARK_PREFIX "%.*llx",
		       p->cmd, BOOKMARK_SUFFIX_LEN, p->label);
	assert(ret > 0 && "failed snprintf");
	ret = write(fd, buf, ret);
	assert(ret > 0 && "failed write");

	lsprintf("Using file '%s' for cmd '%s'\n", p->file, buf);

	/* Clean-up */
	ret = close(fd);
	assert(ret == 0 && "failed close");

	MM_FREE(p);
}

static void run_command(const char *file, const char *cmd, struct hax *h)
{
	struct cmd_packet *p = MM_MALLOC(1, struct cmd_packet);
	assert(p && "failed allocate command packet");

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
	struct agent *a_dest = MM_MALLOC(1, struct agent);
	assert(a_dest != NULL && "failed allocate agent");
	assert(a_src != NULL && "cannot copy null agent");

	a_dest->tid                    = a_src->tid;
	a_dest->action.handling_timer  = a_src->action.handling_timer;
	a_dest->action.context_switch  = a_src->action.context_switch;
	a_dest->action.forking         = a_src->action.forking;
	a_dest->action.sleeping        = a_src->action.sleeping;
	a_dest->action.vanishing       = a_src->action.vanishing;
	a_dest->action.readlining      = a_src->action.readlining;
	a_dest->action.schedule_target = a_src->action.schedule_target;
	assert(memcmp(&a_dest->action, &a_src->action,
		      sizeof(a_dest->action)) == 0 &&
	       "Did you update agent->action without updating save.c?");

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
		if (src->schedule_in_flight == a_src)
			dest->schedule_in_flight = a_dest;
	}
}
static void copy_sched(struct sched_state *dest, const struct sched_state *src)
{
	dest->cur_agent           = NULL;
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
	} else {
		dest->last_vanished_agent = NULL;
	}

	dest->context_switch_pending = src->context_switch_pending;
	dest->context_switch_target  = src->context_switch_target;
	dest->guest_init_done        = src->guest_init_done;
	dest->entering_timer         = src->entering_timer;
}
static void copy_test(struct test_state *dest, const struct test_state *src)
{
	dest->test_is_running = src->test_is_running;
	if (src->current_test == NULL) {
		dest->current_test = NULL;
	} else {
		dest->current_test = MM_STRDUP(src->current_test);
		assert(dest->current_test != NULL && "couldn't strdup test");
	}
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
static void free_arbiter_choices(struct arbiter_state *a)
{
// TODO: deprecate one of these
#if 0
	while (Q_GET_SIZE(&a->choices) > 0) {
		struct choice *c = Q_GET_HEAD(&a->choices);
		Q_REMOVE(&a->choices, c, nobe);
		lsprintf("discarding buffered arbiter choice %d\n", c->tid);
		MM_FREE(c);
	}
#else
	struct choice *c;
	Q_FOREACH(c, &a->choices, nobe) {
		lsprintf("Preserving buffered arbiter choice %d\n", c->tid);
	}
#endif
}

static void free_hax(struct hax *h)
{
	free_sched(h->oldsched);
	MM_FREE(h->oldsched);
	free_test(h->oldtest);
	MM_FREE(h->oldtest);
	h->oldsched = NULL;
	h->oldtest = NULL;
}

/* Reverse that which is not glowing green. */
static void restore_ls(struct ls_state *ls, struct hax *h)
{
	lsprintf("88 MPH: eip 0x%x -> 0x%x; triggers %d -> %d (absolute %d); "
		 "last choice %d\n",
		 ls->eip, h->eip, ls->trigger_count, h->trigger_count,
		 ls->absolute_trigger_count, h->chosen_thread);

	ls->eip           = h->eip;
	ls->trigger_count = h->trigger_count;

	free_sched(&ls->sched);
	copy_sched(&ls->sched, h->oldsched);
	free_test(&ls->test);
	copy_test(&ls->test, h->oldtest);
	free_arbiter_choices(&ls->arbiter);

	ls->just_jumped = true;
}

/******************************************************************************
 * interface
 ******************************************************************************/

void save_init(struct save_state *ss)
{
	ss->root = NULL;
	ss->current = NULL;
	ss->next_tid = -1;
}

void save_recover(struct save_state *ss, struct ls_state *ls, int new_tid)
{
	/* After a longjmp, we will be on exactly the node we jumped to, but
	 * there must be a special call to let us know what our new course is
	 * (see sched_recover). */
	assert(ls->just_jumped);
	ss->next_tid = new_tid;
	lsprintf("explorer chose tid %d; ready for action\n", new_tid);
}

/* In the typical case, this signifies that we have reached a new decision
 * point. We:
 *  - Add a new choice node to signify this
 *  - The TID that was chosen to get here is stored in ss->new_tid; copy that
 *  - Create a list of possible TIDs to explore from this choice point
 *  - Store the new_tid for some subsequent choice point
 */
void save_setjmp(struct save_state *ss, struct ls_state *ls,
		 int new_tid, bool our_choice, bool end_of_test)
{
	struct hax *h;

	lsprintf("tid %d to eip 0x%x, where we %s tid %d\n", ss->next_tid,
		 ls->eip, our_choice ? "choose" : "follow", new_tid);

	/* Whether there should be a choice node in the tree is dependent on
	 * whether the current pending choice was our decision or not. The
	 * explorer's choice (!ours) will be in anticipation of a new node, but
	 * at that point we won't have the info to create the node until we go
	 * one step further. */
	if (our_choice) {
		h = MM_MALLOC(1, struct hax);
		assert(h && "failed allocate choice node");

		h->eip           = ls->eip;
		h->trigger_count = ls->trigger_count;
		h->chosen_thread = ss->next_tid;

		/* Put the choice into the tree. */
		if (ss->root == NULL) {
			/* First/root choice. */
			assert(ss->current == NULL);
			assert(end_of_test || ss->next_tid == -1);

			h->parent = NULL;
			ss->root  = h;
		} else {
			/* Subsequent choice. */
			assert(ss->current != NULL);
			assert(end_of_test || ss->next_tid != -1);

			// XXX: Q_INSERT_TAIL causes a sigsegv
			Q_INSERT_HEAD(&ss->current->children, h, sibling);
			h->parent = ss->current;
		}

		Q_INIT_HEAD(&h->children);
		h->all_explored = end_of_test;
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
		assert(!h->all_explored); /* exploration invariant */
	}

	h->oldsched = MM_MALLOC(1, struct sched_state);
	assert(h->oldsched && "failed allocate oldsched");
	copy_sched(h->oldsched, &ls->sched);

	h->oldtest = MM_MALLOC(1, struct test_state);
	assert(h->oldtest && "failed allocate oldtest");
	copy_test(h->oldtest, &ls->test);

	ss->current  = h;
	ss->next_tid = new_tid;

	run_command(ls->cmd_file, CMD_BOOKMARK, (lang_void *)h);
}

void save_longjmp(struct save_state *ss, struct ls_state *ls, struct hax *h)
{
	struct hax *rabbit = ss->current;

	assert(ss->root != NULL && "Can't longjmp with no decision tree!");
	assert(ss->current != NULL);

	/* The caller is allowed to say NULL, which means jump to the root. */
	if (h == NULL)
		h = ss->root;

	/* Find the target choice point from among our ancestors. */
	while (ss->current != h) {
		/* This node will soon be in the future. Reclaim memory. */
		free_hax(ss->current);
		run_command(ls->cmd_file, CMD_DELETE, (lang_void *)h);

		ss->current = ss->current->parent;

		/* We won't have simics bookmarks, or indeed some saved state,
		 * for non-ancestor choice points. */
		assert(ss->current != NULL && "Can't jump to a non-ancestor!");

		/* the tree must not have properties of Philip J. Fry */
		if (rabbit) rabbit = rabbit->parent;
		if (rabbit) rabbit = rabbit->parent;
		assert(rabbit != ss->current && "somehow, a cycle?!?");
	}

	restore_ls(ls, h);

	run_command(ls->cmd_file, CMD_SKIPTO, (lang_void *)h);
}
