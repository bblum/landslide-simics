/**
 * @file save.h
 * @brief choice tree tracking, incl. save/restore
 * @author Ben Blum
 */

#ifndef __LS_SAVE_H
#define __LS_SAVE_H

#include "variable_queue.h"

struct ls_state;
struct sched_state;
struct test_state;

// TODO: examine the attributes!

/* Represents a single choice point in the decision tree.
 * The data here stored actually reflects the state upon the *completion* of
 * that choice; i.e., when the next choice has to be made. */
struct hax {
	int eip;           /* The eip for the *next* choice point. */
	int trigger_count; /* from ls_state */
	int chosen_thread; /* TID that was chosen to get here. -1 if root. */

	/* Saved state from the past. The state struct pointers are non-NULL if
	 * it's a point directly backwards from where we are. */
	struct sched_state *oldsched;
	struct test_state *oldtest;
	/* List of things that are *not* saved/restored (i.e., glowing green):
	 *  - arbiter_state (just a choice queue, maintained internally)
	 *  - ls_state's absolute_trigger_count (obv.)
	 *  - save_state (duh)
	 */

	/* Tree link data. */
	struct hax *parent;
	Q_NEW_LINK(struct hax) sibling;
	Q_NEW_HEAD(struct, struct hax) children;

	/* Note: a list of available tids to run next is implicit in the copied
	 * sched! (TODO: is this sufficient, if sched gets wiped sometimes?) */
};

struct save_state {
	/* The root of the decision tree, or NULL if save_setjmp() was never
	 * called. */
	struct hax *root;
	/* If root is set, this points to the "current" node in the tree */
	struct hax *current;
	struct { int tid; int ours; } next_choice;
};

void save_init(struct save_state *);

/* Current state, and the next_tid/our_choice is about the next in-flight
 * choice. */
void save_setjmp(struct save_state *, struct ls_state *,
		 int next_tid, bool our_choice);

/* If hax is NULL, then longjmps to the root. Otherwise, hax must be between
 * the current choice point and the root (inclusive). */
void save_longjmp(struct save_state *, struct ls_state *, struct hax *);

#endif
