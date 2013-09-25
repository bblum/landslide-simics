/**
 * @file estimate.c
 * @brief online state space size estimation
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#define MODULE_NAME "ESTIMATE"
#define MODULE_COLOUR COLOUR_DARK COLOUR_CYAN

#include "common.h"
#include "explore.h"
#include "schedule.h"
#include "tree.h"
#include "variable_queue.h"

static bool is_child_marked(struct hax *h, struct agent *a) {
	struct hax *child;

	/* A marked child is one that we have already explored, or one we wish
	 * to explore. In short, one we know will be in the tree eventually. */
	if (a->do_explore) {
		return true;
	}
	Q_FOREACH(child, &h->children, sibling) {
		if (child->chosen_thread == a->tid) {
			return true;
		}
	}
	return false;
}

void estimate(struct hax *root, struct hax *current)
{
	/* The estimated proportion of this branch, which we will accumulate. */
	long double this_nobe_proportion = 1.0L;

	/* When we need to retroactively fix a nobe's probability, we need to
	 * subtract the old value and add the new value to its parent too. */
	long double nobe_delta = 0.0L;

	/* Stage 1 -- figure out this branch's probability to begin with. We
	 * can also retroactively fix-up the probabilities for changed parent
	 * nobes along the way. (Skip the terminal node itself, obviously.) */
	for (struct hax *h = current->parent; h != NULL; h = h->parent) {
		if (h->parent == NULL) { assert(h == root); }

		/* Step 1-1 -- Compute Marked(vi) product term.  */

		/* save value computed last time a branch was here */
		h->marked_children_old = h->marked_children;

		h->marked_children = 0;
		struct agent *a;
		FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
			if (is_child_marked(h, a)) {
				h->marked_children++;
			}
		);
		assert(h->marked_children > 0); /* we should be there at least */

		/* p = product_vi 1/(Marked(vi) + F(vi)).
		 * TODO: explore F(vi) != 0, i.e., not-lazy-strategy */
		this_nobe_proportion /= h->marked_children;

		/* Step 1-2 -- Retroactively fix-up past branch probabilities. */

		/* Stash old proportion value so we can compute the delta. */
		long double old_proportion = h->proportion;

		/* Adjust this nobe's probability by the nobe delta, in case its
		 * child changed. Note: MUST happen before dividing out a
		 * possible changed number of marked children, because that
		 * factor will also need to affect this nobe delta. */
		h->proportion += nobe_delta;

		/* Readjust in case a new child was marked of this nobe. */
		h->proportion *= h->marked_children_old;
		h->proportion /= h->marked_children;

		/* Save our delta for the next loop iteration on our parent. */
		nobe_delta = h->proportion - old_proportion;
	}

	/* Stage 2 -- Add this branch's final proportion value to all parents.
	 * Independently, here we also accumulate the total time and branches
	 * for the enclosing subtree starting at each parent nobe. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
		if (h->parent == NULL) { assert(h == root); }
		// TODO: propagate other stuff
		h->proportion += this_nobe_proportion;

	}

	lsprintf(BRANCH, "Estimate: %Lf\n", root->proportion);
}
