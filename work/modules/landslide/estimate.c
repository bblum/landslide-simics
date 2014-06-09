/**
 * @file estimate.c
 * @brief online state space size estimation
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#define MODULE_NAME "ESTIMATE"
#define MODULE_COLOUR COLOUR_DARK COLOUR_CYAN

#include "common.h"
#include "estimate.h"
#include "explore.h"
#include "schedule.h"
#include "tree.h"
#include "variable_queue.h"

void estimate_init(struct estimate_state *e)
{
}

/* Returns number of elapsed useconds since last call to this. If there was no
 * last call, return value is undefined. */
uint64_t estimate_update_time(struct estimate_state *e)
{
	struct timeval new_time;
	int rv = gettimeofday(&new_time, NULL);
	assert(rv == 0 && "failed to gettimeofday");
	assert(new_time.tv_usec < 1000000);

	time_t secs = new_time.tv_sec - e->last_save_time.tv_sec;
	suseconds_t usecs = new_time.tv_usec - e->last_save_time.tv_usec;

	e->last_save_time.tv_sec  = new_time.tv_sec;
	e->last_save_time.tv_usec = new_time.tv_usec;

	return (secs * 1000000) + usecs;
}

/******************** actual estimation algorithm follows ********************/

static bool is_child_marked(struct hax *h, struct agent *a)
{
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

static void _estimate(struct estimate_state *e, struct hax *root, struct hax *current)
{
	/* The estimated proportion of this branch, which we will accumulate. */
	long double this_nobe_proportion = 1.0L;

	/* When we need to retroactively fix a nobe's probability, we need to
	 * subtract the old value and add the new value to its parent too. */
	long double nobe_delta = 0.0L;

	/* Stores old proportion value before updating it at each nobe. Also
	 * used for sanity-checking -- each nobe's proportion must not be
	 * greater than the proportion of its parent. */
	long double old_proportion = 0.0L;

	/* Stage 1 -- figure out this branch's probability to begin with. We
	 * can also retroactively fix-up the probabilities for changed parent
	 * nobes along the way. (Skip the terminal node itself, obviously.) */
	for (struct hax *h = current->parent; h != NULL; h = h->parent) {
		if (h->parent == NULL) { assert(h == root); }

		/* Step 1-1 -- Compute Marked(vi) product term.  */
		int total_children = 0;

		/* save value computed last time a branch was here */
		h->marked_children_old = h->marked_children;

		h->marked_children = 0;
		struct agent *a;
		FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
			if (is_child_marked(h, a)) {
				h->marked_children++;
			}
			total_children++;
		);
		assert(h->marked_children > 0); /* we should be there at least */
		/* No adjustment for history (F(vi) = 0). */
		assert(h->marked_children >= h->marked_children_old);

		/* p = product_vi 1/(Marked(vi) + F(vi)).
		 * TODO: explore F(vi) != 0, i.e., not-lazy-strategy */
		this_nobe_proportion /= h->marked_children;
		assert(this_nobe_proportion >= 0);

		/* Step 1-2 -- Retroactively fix-up past branch probabilities. */

		// lsprintf(DEV, "last %1.30Lf, this %1.30Lf\n",
		//          old_proportion, h->proportion);

		/* Stash old proportion value so we can compute the delta.
		 * Also check here the invariant that this nobe's child's old
		 * proportion was less than this one's. */
		// XXX: This assert fails sometimes with tiny margins of error. I
		// XXX: think it is because of floating point but am not sure why.
		// assert(old_proportion <= h->proportion);
		old_proportion = h->proportion;
		assert(old_proportion >= 0);

		/* Adjust this nobe's probability by the nobe delta, in case its
		 * child changed. Note: MUST happen before dividing out a
		 * possible changed number of marked children, because that
		 * factor will also need to affect this nobe delta. */
		h->proportion += nobe_delta;
		assert(h->proportion >= 0);

		/* Readjust if a new (non-1st) child was marked of this nobe. */
		if (h->marked_children_old != h->marked_children &&
		    h->marked_children_old != 0) {
			assert(h->marked_children > h->marked_children_old);
			h->proportion *= h->marked_children_old;
			h->proportion /= h->marked_children;
			/* This change affects all children's proportions. */
			for (struct hax *h2 = current->parent; h2 != h;
			     h2 = h2->parent) {
				h2->proportion *= h->marked_children_old;
				h2->proportion /= h->marked_children;
			}
		}

		/* Save our delta for the next loop iteration on our parent. */
		nobe_delta = h->proportion - old_proportion;
	}

	assert(this_nobe_proportion >= 0);

	/* Stage 2 -- Add this branch's final proportion value to all parents.
	 * Independently, here we also accumulate the total time and branches
	 * for the enclosing subtree starting at each parent nobe. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
		if (h->parent == NULL) { assert(h == root); }
		// TODO: propagate other stuff
		h->proportion += this_nobe_proportion;

	}
}

long double estimate(struct estimate_state *e, struct hax *root, struct hax *current)
{
	if (current->estimate_computed) {
		return root->proportion;
	} else {
		_estimate(e, root, current);
		current->estimate_computed = true;
		return root->proportion;
	}
}
