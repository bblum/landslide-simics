/**
 * @file save.c
 * @brief choice tree tracking, incl. save/restore
 * @author Ben Blum
 */

#include <assert.h>
#include <stdlib.h> /* for mktemp */
#include <string.h> /* for memcmp, strlen */
#include <unistd.h> /* for write, unlink */

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

/******************************************************************************
 * helpers
 ******************************************************************************/

/* The bookmark name is this prefix concatenated with the hexadecimal address
 * of the choice struct (no "0x"). */
#define BOOKMARK_PREFIX "landslide"
#define BOOKMARK_SUFFIX_LEN ((int)(2*sizeof(unsigned long long))) /* for hex */
#define BOOKMARK_MAX_LEN (strlen(BOOKMARK_PREFIX) + 16)

#define CMD_BOOKMARK "set-bookmark"
#define CMD_SKIPTO   "skip-to"
#define CMD_BUF_LEN 64

static void run_command(const char *which_cmd, unsigned long long label)
{
	// this use of run_command_file is deprecated; TODO delete
#if 0
	char filename[] = "/tmp/landslide-cmd-XXXXXX";
	char buf[CMD_BUF_LEN];
	int ret;
	int fd = mkstemp(filename);
	assert(fd != -1 && "mktemp failed create command file");

	/* Generate command */
	STATIC_ASSERT(CMD_BUF_LEN > MAX(strlen(CMD_BOOKMARK),strlen(CMD_SKIPTO))
				    + 1 + BOOKMARK_MAX_LEN + 1);
	ret = snprintf(buf, CMD_BUF_LEN, "%s " BOOKMARK_PREFIX "%.*llx\n",
		       which_cmd, BOOKMARK_SUFFIX_LEN, label);
	assert(ret > 0 && "failed snprintf");
	ret = write(fd, buf, ret);
	assert(ret > 0 && "failed write");

	lsprintf("Using temp file '%s' for cmd '%s'\n", filename, buf);
	SIM_run_command_file(filename, true /* ?!? */);

	/* Clean-up */
	ret = close(fd);
	assert(ret == 0 && "failed close");
	ret = unlink(filename);
	assert(ret == 0 && "failed unlink");
#else
	int ret;
	char buf[CMD_BUF_LEN];
	STATIC_ASSERT(CMD_BUF_LEN > MAX(strlen(CMD_BOOKMARK),strlen(CMD_SKIPTO))
				    + 1 + BOOKMARK_MAX_LEN + 1);
	ret = snprintf(buf, CMD_BUF_LEN, "%s " BOOKMARK_PREFIX "%.*llx\n",
		       which_cmd, BOOKMARK_SUFFIX_LEN, label);
	assert(ret > 0 && "failed snprintf");
	SIM_run_command(buf);
#endif
}
static void bookmark(lang_void *addr)
{
	run_command(CMD_BOOKMARK, (unsigned long long)addr);
}
static void skip_to(lang_void *addr)
{
	run_command(CMD_SKIPTO, (unsigned long long)addr);
}

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
	dest->current_test    = MM_STRDUP(src->current_test);
	assert(dest->current_test != NULL && "couldn't strdup current_test");
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
	while (Q_GET_SIZE(&a->choices) > 0) {
		struct choice *c = Q_GET_HEAD(&a->choices);
		Q_REMOVE(&a->choices, c, nobe);
		lsprintf("discarding buffered arbiter choice %d\n", c->tid);
		MM_FREE(c);
	}
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
}

/******************************************************************************
 * interface
 ******************************************************************************/

void save_init(struct save_state *ss)
{
	ss->root = NULL;
	ss->current = NULL;
	ss->next_choice.tid = -1;
	ss->next_choice.ours = true;
}

/* In the typical case,, this signifies that we have reached a new decision
 * point. We:
 *  - Add a new choice node to signify this
 *  - The TID that was chosen to get here is stored in ss->new_tid; copy that
 *  - Create a list of possible TIDs to explore from this choice point
 *  - Store the new_tid for some subsequent choice point
 */
void save_setjmp(struct save_state *ss, struct ls_state *ls,
		 int new_tid, bool our_choice)
{
	struct hax *h;

	lsprintf("%s tid %d to eip 0x%x, where we %s tid %d\n",
		 ss->next_choice.ours ? "chose" : "followed",
		 ss->next_choice.tid, ls->eip,
		 our_choice ? "choose" : "follow", new_tid);

	/* Whether there should be a choice node in the tree is dependent on
	 * whether the *previous* pending choice was our decision or not. */
	if (ss->next_choice.ours) {
		h = MM_MALLOC(1, struct hax);
		assert(h && "failed allocate choice node");

		assert(our_choice && "!our_choice after previous our_choice??");

		h->eip           = ls->eip;
		h->trigger_count = ls->trigger_count;
		h->chosen_thread = ss->next_choice.tid;

		/* Put the choice into the tree. */
		if (ss->root == NULL) {
			/* First/root choice. */
			assert(ss->current == NULL);
			assert(ss->next_choice.tid == -1);
			assert(ss->next_choice.ours == true);

			h->parent = NULL;
			ss->root  = h;
		} else {
			/* Subsequent choice. */
			assert(ss->current != NULL);
			assert(ss->next_choice.tid != -1);

			// XXX: Q_INSERT_TAIL causes a sigsegv
			Q_INSERT_HEAD(&ss->current->children, h, sibling);
			h->parent = ss->current;
		}

		Q_INIT_HEAD(&h->children);
	} else {
		assert(ss->root != NULL);
		assert(ss->current != NULL);
		assert(ss->next_choice.tid != -1);

		/* Find already-existing previous choice nobe */
		Q_SEARCH(h, &ss->current->children, sibling,
			 h->chosen_thread == ss->next_choice.tid);
		assert(h != NULL && "!our_choice but chosen tid not found...");

		assert(h->eip == ls->eip);
		assert(h->trigger_count == ls->trigger_count);
		assert(h->oldsched == NULL);
		assert(h->oldtest == NULL);
	}

	h->oldsched = MM_MALLOC(1, struct sched_state);
	assert(h->oldsched && "failed allocate oldsched");
	copy_sched(h->oldsched, &ls->sched);

	h->oldtest = MM_MALLOC(1, struct test_state);
	assert(h->oldtest && "failed allocate oldtest");
	copy_test(h->oldtest, &ls->test);

	ss->current = h;

	ss->next_choice.tid  = new_tid;
	ss->next_choice.ours = our_choice;

	SIM_run_alone(bookmark, (lang_void *)h);
}

void save_longjmp(struct save_state *ss, struct ls_state *ls, struct hax *h)
{
	struct hax *rabbit = ss->current;

	assert(ss->root != NULL && "Can't longjmp with no decision tree!");
	assert(ss->current != NULL);

	/* The caller is allowed to say NULL, which means jump to the root. */
	if (h == NULL)
		h = ss->root;
	
	// TODO: Here, analyse the tree and get some choices for the next traversal.

	/* Find the target choice point from among our ancestors. */
	while (ss->current != h) {
		/* This node will soon be in the future. Reclaim memory. */
		free_hax(ss->current);

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

	// TODO: Here, append one or more arbiter_choices (has to be after restore_ls)

	/* Finally, reclaim memory from the node we just skipped to, since we
	 * will soon be asked to setjmp on it again with !our_choice. */
	free_hax(h);

	SIM_run_alone(skip_to, (lang_void *)h);
}