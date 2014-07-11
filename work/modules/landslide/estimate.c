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
#include "landslide.h"
#include "schedule.h"
#include "tree.h"
#include "variable_queue.h"

/******************************************************************************
 * estimation algorithm / logic
 ******************************************************************************/

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

static void _estimate(struct hax *root, struct hax *current)
{
	/* The estimated proportion of this branch, which we will accumulate. */
	long double this_nobe_proportion = 1.0L;

	/* The recorded proportion of each ancestor nobe is always equal to the
	 * sum of the proportions of all its children that we already visited.
	 * Whenever we adjust the proportion of a nobe whose marked children
	 * changed, this stores how much it changed across loop iterations so
	 * we can adjust its parent's proportion by the same amount. */
	long double child_proportion_delta = 0.0L;

	/* For estimating total exploration time rather than number of branches.
	 * While the proportion represents only branches we've already explored,
	 * the time at each nobe is an estimate of the total time it would take
	 * to explore that nobe's entire subtree (as if that nobe were the
	 * root). This lets us avoid double-counting ancestor transitions. */
	long double child_subtree_usecs_delta = 0.0L;
	/* while individual nobe's usecs should never exceed uint64_t range,
	 * estimates for an entire subtree may require double precision. */
	long double child_usecs = (long double)current->usecs;
	bool new_subtree = true;

	/* Stage 1 -- figure out this branch's proportion to begin with. We
	 * can also retroactively fix-up the proportions for changed parent
	 * nobes along the way. (Skip the terminal node itself, obviously.) */
	for (struct hax *h = current->parent; h != NULL; h = h->parent) {
		if (h->parent == NULL) { assert(h == root); }
		assert(!h->estimate_computed);

		/* Step 1-1 -- Accumulate this nobe's total proportion, which
		 * is the product of all its ancestors' proportions; i.e.:
		 * p = Product_{vi <- all ancestors} 1/Marked(vi) */
		int old_marked_children = update_marked_children(h);
		this_nobe_proportion /= h->marked_children;
		ASSERT_FRACTIONAL(this_nobe_proportion);

		/* Step 1-2 -- Retroactively fix-up past branch proportions. */

		/* Save old proportion of this nobe for computing the delta. */
		ASSERT_FRACTIONAL(h->proportion);
		long double old_proportion = h->proportion;

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

		/* Bonus Step -- Estimate subtree exploration time. */

		bool child_was_new_subtree = false; /* only true once */
		unsigned int num_explored_children = Q_GET_SIZE(&h->children);
		if (new_subtree && num_explored_children > 1) {
			new_subtree = false;
			child_was_new_subtree = true;
		}

		uint64_t old_usecs = h->subtree_usecs;
		if (new_subtree) {
			/* This nobe is part of a completely new subtree. */
			assert(h->subtree_usecs == 0.0L);
			assert(old_marked_children == 0);
			assert(num_explored_children == 1);
			/* Estimate subtree time from scratch. */
			h->subtree_usecs = child_usecs + child_subtree_usecs_delta;
		} else {
			/* This nobe existed before this branch, and a subtree
			 * time estimate was already computed for it. */
			assert(h->subtree_usecs != 0);
			assert(old_marked_children != 0);

			/* undo the averaging operation previously done */
			h->subtree_usecs /= old_marked_children;

			if (child_was_new_subtree) {
				h->subtree_usecs *= num_explored_children - 1;
				/* Child nobe newly appeared. Child transition
				 * usecs needs to be incorporated. */
				h->subtree_usecs += child_usecs;
			} else {
				h->subtree_usecs *= num_explored_children;
				/* Child transition usecs was already accounted
				 * for; need only adjust by subtree delta. */
				// Do nothing.
			}

			h->subtree_usecs += child_subtree_usecs_delta;
		}

		/* Note that at this point subtree_usecs represents neither the
		 * actual number of usecs spent exploring this subtree, nor the
		 * estimated eventual total. Rather, it is the total for all
		 * subtrees explored so far of the estimated total for each. */

		/* Compute average usecs per child subtree and multiply
		 * by the expected number of expected children to get
		 * expected total subtree size. */
		h->subtree_usecs /= num_explored_children;
		h->subtree_usecs *= h->marked_children;

		// FIXME: Can probably clean-up above logic by dealing with
		// child new subtree here, rather than above, by deciding
		// whether to incorporate child usecs into the delta or not.

		/* Save exploration time of this subtree for next iteration. */
		child_usecs = (long double)h->usecs;
		child_subtree_usecs_delta = h->subtree_usecs - old_usecs;
	}

	ASSERT_FRACTIONAL(this_nobe_proportion);

	/* Stage 2 -- Add this branch's final proportion value to all parents. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
		h->proportion += this_nobe_proportion;

	}
}

long double estimate_time(struct hax *root, struct hax *current)
{
	if (!current->estimate_computed) {
		_estimate(root, current);
		current->estimate_computed = true;
	}
	return (long double)root->usecs + root->subtree_usecs;
}

long double estimate_proportion(struct hax *root, struct hax *current)
{
	if (!current->estimate_computed) {
		_estimate(root, current);
		current->estimate_computed = true;
	}
	return root->proportion;
}

/******************************************************************************
 * pretty-printing / convenience
 ******************************************************************************/

struct human_friendly_time { uint64_t secs, mins, hours, days, years; };

static void human_friendly_time(long double usecs, struct human_friendly_time *hft)
{
	hft->years = 0;
	hft->days = 0;
	hft->hours = 0;
	hft->mins = 0;
	hft->secs = (uint64_t)(usecs / 1000000);
	if (hft->secs >= 60) {
		hft->mins = hft->secs / 60;
		hft->secs = hft->secs % 60;
	}
	if (hft->mins >= 60) {
		hft->hours = hft->mins / 60;
		hft->mins  = hft->mins % 60;
	}
	if (hft->hours >= 24) {
		hft->days  = hft->hours / 24;
		hft->hours = hft->hours % 24;
	}
	if (hft->days >= 365) {
		hft->years = hft->days / 365;
		hft->days  = hft->days % 365;
	}
}

static void print_human_friendly_time(verbosity v, struct human_friendly_time *hft)
{
	if (hft->years != 0)
		printf(v, "%luy ", hft->years);
	if (hft->days  != 0)
		printf(v, "%lud ", hft->days);
	if (hft->hours != 0)
		printf(v, "%luh ", hft->hours);
	if (hft->mins  != 0)
		printf(v, "%lum ", hft->mins);

	printf(v, "%lus", hft->secs);
}

void print_estimates(struct ls_state *ls)
{
	long double proportion = estimate_proportion(ls->save.root, ls->save.current);
	long double usecs = estimate_time(ls->save.root, ls->save.current);

	lsprintf(BRANCH, COLOUR_BOLD COLOUR_GREEN
		 "Estimate: %Lf%% (%Lf total branches)\n" COLOUR_DEFAULT,
		 proportion * 100, (ls->save.total_jumps + 1) / proportion);

	struct human_friendly_time total_time, elapsed_time, remaining_time;
	human_friendly_time(usecs, &total_time);
	human_friendly_time(ls->save.total_usecs, &elapsed_time);
	human_friendly_time(usecs - (long double)ls->save.total_usecs,
			    &remaining_time);

	lsprintf(BRANCH, COLOUR_BOLD COLOUR_GREEN "Estimated time: ");
	print_human_friendly_time(BRANCH, &total_time);
	printf(BRANCH, " (elapsed ");
	print_human_friendly_time(BRANCH, &elapsed_time);
	printf(BRANCH, "; remaining ");
	print_human_friendly_time(BRANCH, &remaining_time);
	printf(BRANCH, ")\n" COLOUR_DEFAULT);

	lsprintf(DEV, COLOUR_BOLD COLOUR_GREEN "Estimated time: "
		 "%Lfs (elapsed %Lfs; remain %Lfs)\n",
		 usecs / 1000000, (long double)ls->save.total_usecs / 1000000,
		 (usecs - (long double)ls->save.total_usecs) / 1000000);
}
