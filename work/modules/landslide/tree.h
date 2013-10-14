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
struct mem_state;
struct sched_state;
struct test_state;

/* Represents a single choice point in the decision tree.
 * The data here stored actually reflects the state upon the *completion* of
 * that choice; i.e., when the next choice has to be made. */
struct hax {
	int eip;           /* The eip for the *next* choice point. */
	unsigned long trigger_count; /* from ls_state */
	int chosen_thread; /* TID that was chosen to get here. -1 if root. */

	/* Saved state from the past. The state struct pointers are non-NULL if
	 * it's a point directly backwards from where we are. */
	struct sched_state *oldsched;
	struct test_state *oldtest;
	struct mem_state *oldmem;
	/* List of things that are *not* saved/restored (i.e., glowing green):
	 *  - arbiter_state (just a choice queue, maintained internally)
	 *  - ls_state's absolute_trigger_count (obv.)
	 *  - save_state (duh)
	 */

	/* Tree link data. */
	struct hax *parent;
	int depth; /* starts at 0 */
	Q_NEW_LINK(struct hax) sibling;
	Q_NEW_HEAD(struct, struct hax) children;

	/* Other transitions (ancestors) that conflict with or happen-before
	 * this one. The length of each array is given by 'depth'. */
	bool *conflicts;      /* if true, then they aren't independent. */
	bool *happens_before; /* "happens_after", really. */

	/* Estimation state */

	/* how many children of this are marked. this can represent an out-of-date
	 * value, which is then used to reassess the probability of this branch. */
	long double marked_children;
	long double marked_children_old; /* old value, before recomputation */
	/* how much time this transition took. value is set by the 'save' module
	 * when the struct is generated, and used by estimation. */
	uint64_t usecs;
	uint64_t cum_usecs; /* cumulative version of above, from root */
	/* the estimated proportion of the tree that the branches in this node's
	 * subtree represent (the "probability" that a random exploration will take
	 * this branch). */
	long double proportion;
	/* cumulative sum of the usecs/branches in this nobe's subtree */
	uint64_t subtree_usecs;
	unsigned long subtree_branches;

	/* Note: a list of available tids to run next is implicit in the copied
	 * sched! Also, the "tags" that POR uses to denote to-be-explored
	 * siblings are in the agent structs on the scheduler queues. */

	bool all_explored;

	char *stack_trace;
};

#endif
