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

/* returns old value */
static int update_marked_children(struct hax *h)
{
	/* save the value that was computed last time */
	int old_marked_children = h->marked_children;

	h->marked_children = 0;
	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
		if (is_child_marked(h, a)) {
			h->marked_children++;
		}
	);
	/* since this is our ancestor, it must at least have us as a child */
	assert(h->marked_children > 0);
	/* since we never use a speculative F(vi) value (other than 0), a nobe's
	 * marked children is always nondecreasing across estimates. */
	assert(h->marked_children >= old_marked_children);

	return old_marked_children;
}

#define ASSERT_FRACTIONAL(val) do {			\
		typeof(val) __val = (val);		\
		assert(__val >= 0.0L && __val <= 1.0L);	\
	} while (0)

static void _estimate(struct estimate_state *e, struct hax *root, struct hax *current)
{
	/* The estimated proportion of this branch, which we will accumulate. */
	long double this_nobe_proportion = 1.0L;

	/* When we need to retroactively fix a nobe's proportion, we need to
	 * subtract the old value and add the new value to its parent too. */
	long double child_proportion_delta = 0.0L;

	/* Stores old proportion value before updating it at each nobe. Also
	 * used for sanity-checking -- each nobe's proportion must not be
	 * greater than the proportion of its parent. */
	long double old_proportion = 0.0L;

	/* Stage 1 -- figure out this branch's proportion to begin with. We
	 * can also retroactively fix-up the proportions for changed parent
	 * nobes along the way. (Skip the terminal node itself, obviously.) */
	for (struct hax *h = current->parent; h != NULL; h = h->parent) {
		if (h->parent == NULL) { assert(h == root); }

		/* Step 1-1 -- Accumulate this nobe's total proportion, which
		 * is the product of all its ancestors' proportions; i.e.:
		 * p = Product_{vi <- all ancestors} 1/Marked(vi) */
		int old_marked_children = update_marked_children(h);
		this_nobe_proportion /= h->marked_children;
		ASSERT_FRACTIONAL(this_nobe_proportion);

		/* Step 1-2 -- Retroactively fix-up past branch proportions. */

		/* Save old proportion of this nobe for computing the delta. */
		ASSERT_FRACTIONAL(h->proportion);
		old_proportion = h->proportion;

		/* If the proportion of the child we just came from (last loop
		 * iteration) changed, update this nobe's proportion to reflect
		 * it. Note that this must happen before dividing out the
		 * changed number of marked children, because that factor also
		 * needs to be applied to this delta. */
		h->proportion += child_proportion_delta;
		ASSERT_FRACTIONAL(h->proportion);

		/* If a new (non-1st) child was marked for this nobe, we need to
		 * readjust the proportions for it and for all its descendants.
		 * Previously, the proportions were computed by dividing by the
		 * old marked children value. So for this and each descendant,
		 * replace that divisor with the new value, by multiplying
		 * through the old value and re-dividing with the new one. */
		if (old_marked_children != h->marked_children &&
		    old_marked_children != 0) {
			assert(h->marked_children > old_marked_children);
			struct hax *h2 = current;
			do {
				h2 = h2->parent;
				assert(h2 != NULL);
				h2->proportion *= old_marked_children;
				h2->proportion /= h->marked_children;
				ASSERT_FRACTIONAL(h2->proportion);
			} while (h2 != h);
		}

		/* Save our delta for the next loop iteration on our parent. */
		child_proportion_delta = h->proportion - old_proportion;
	}

	ASSERT_FRACTIONAL(this_nobe_proportion);

	/* Stage 2 -- Add this branch's final proportion value to all parents.
	 * Independently, here we also accumulate the total time for the
	 * enclosing subtree starting at each parent nobe. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
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
