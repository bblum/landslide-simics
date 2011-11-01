/**
 * @file tree.h
 * @brief choice tree tracking
 * @author Ben Blum
 */

#ifndef __LS_TREE_H
#define __LS_TREE_H

#include <simics/api.h> /* for "bool" */

#include "variable_queue.h"

struct ls_state;
struct sched_state;
struct test_state;

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
	int depth; /* for stat counting */
	Q_NEW_LINK(struct hax) sibling;
	Q_NEW_HEAD(struct, struct hax) children;

	/* Note: a list of available tids to run next is implicit in the copied
	 * sched! (TODO: is this sufficient, if sched gets wiped sometimes?) */

	bool all_explored;
};

#endif
