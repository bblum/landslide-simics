/**
 * @file estimate.c
 * @brief online state space size estimation
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define MODULE_NAME "ESTIMATE"
#define MODULE_COLOUR COLOUR_DARK COLOUR_CYAN

#include "common.h"
#include "estimate.h"
#include "explore.h"
#include "landslide.h"
#include "messaging.h"
#include "schedule.h"
#include "tree.h"
#include "variable_queue.h"

uint64_t update_time(struct timeval *tv)
{
	struct timeval new_time;
	int rv = gettimeofday(&new_time, NULL);
	assert(rv == 0 && "failed to gettimeofday");
	assert(new_time.tv_usec < 1000000);

	time_t secs = new_time.tv_sec - tv->tv_sec;
	suseconds_t usecs = new_time.tv_usec - tv->tv_usec;

	tv->tv_sec  = new_time.tv_sec;
	tv->tv_usec = new_time.tv_usec;

	return (secs * 1000000) + usecs;
}

static void fudge_time(struct timeval *tv, uint64_t time_asleep)
{
	/* Suppose our last save time was X, and our new save time will be X+Y.
	 * But between there we got suspended for Z time which we don't want to
	 * count among that. To make the next update_time() call produce Y-Z as
	 * the result, fudge X to be X+Z instead. X+Y - (X+Z) = Y-Z. */
	time_t secs_asleep = time_asleep / 1000000;
	time_t usecs_asleep = time_asleep % 1000000;
	tv->tv_sec += secs_asleep;
	tv->tv_usec += usecs_asleep;
}

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
static unsigned int update_marked_children(struct hax *h)
{
	/* save the value that was computed last time */
	unsigned int old_marked_children = h->marked_children;

	h->marked_children = 0;
	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
		if (is_child_marked(h, a)) {
			h->marked_children++;
		}
	);
	/* ezpz */
	if (h->xbegin) {
		h->marked_children += ARRAY_LIST_SIZE(&h->xabort_codes_ever);
	}
	/* since this is our ancestor, it must at least have us as a child */
	assert(h->marked_children > 0);
	/* since we never use a speculative F(vi) value (other than 0), a nobe's
	 * marked children is always nondecreasing across estimates. */
	assert(h->marked_children >= old_marked_children);

	return old_marked_children;
}

#ifdef PREEMPT_EVERYWHERE
// FIXME: bug #218
#define ASSERT_FRACTIONAL(val) do { } while (0)
#else
#define ASSERT_FRACTIONAL(val) do {			\
		typeof(val) __val = (val);		\
		assert(__val >= 0.0L && __val <= 1.0L);	\
	} while (0)
#endif

/* Propagate changed proportion to descendants, including the nobe itself. */
static void adjust_subtree_proportions(struct hax *ancestor, struct hax *leaf,
				       unsigned int old_marked_children,
				       unsigned int new_marked_children)
{
	struct hax *h = leaf;
	do {
		h = h->parent;
		assert(h != NULL);
		/* It's ok if some descendants are new (i.e. never estimated
		 * before) as their proportion will just be 0. */
		h->proportion *= old_marked_children;
		h->proportion /= new_marked_children;
		ASSERT_FRACTIONAL(h->proportion);
	} while (h != ancestor);
}

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
		unsigned int old_marked_children = update_marked_children(h);
		this_nobe_proportion /= h->marked_children;
		ASSERT_FRACTIONAL(this_nobe_proportion);

		if (h->marked_children > 1) { assert(h->is_preemption_point); }

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
			adjust_subtree_proportions(h, current, old_marked_children,
						   h->marked_children);
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

		long double old_usecs = h->subtree_usecs;
		if (new_subtree) {
			/* This nobe is part of a completely new subtree. */
			assert(h->subtree_usecs == 0.0L);
			assert(old_marked_children == 0);
			assert(num_explored_children == 1);
			/* Estimate subtree time from scratch. */
			h->subtree_usecs = child_usecs + child_subtree_usecs_delta;
			lsprintf(INFO, "est #%d/tid%d new subtree",
				 h->depth, h->chosen_thread);
		} else {
			/* This nobe existed before this branch, and a subtree
			 * time estimate was already computed for it. */
			assert(h->subtree_usecs != 0);
			assert(old_marked_children != 0);

			/* undo the averaging operation previously done */
			long double old_subtree_usecs = h->subtree_usecs;
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
			lsprintf(INFO, "est #%d/tid%d notnew, old usecs %Lf, "
				 "OMC %u", h->depth, h->chosen_thread,
				 old_subtree_usecs, old_marked_children);
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

		printf(INFO, ", MC %lu, numexplored %u, usecs %Lf\n",
		       h->marked_children, num_explored_children,
		       h->subtree_usecs);
	}

	ASSERT_FRACTIONAL(this_nobe_proportion);

	/* Stage 2 -- Add this branch's final proportion value to all parents. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
		h->proportion += this_nobe_proportion;

	}
}

/* FIXME: With KEEP_RUNNING_YIELDING_THREADS, this might be dead functionality. */
void untag_blocked_branch(struct hax *ancestor, struct hax *leaf, struct agent *a,
			  bool was_ancestor)
{
	assert(a->do_explore);

	if (was_ancestor) {
		/* We shouldn't be in this subtree at all. We can't untag this
		 * edge, as we've already gone down it, but we can untag any
		 * other not-yet-explored tagged edges within the subtree. */
		unsigned int last_chosen_thread = leaf->chosen_thread;
		for (struct hax *h = leaf->parent; h != ancestor; h = h->parent) {
			struct agent *a;
			FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
				a->user_yield.blocked = true;
				if (a->do_explore && a->tid != last_chosen_thread) {
					untag_blocked_branch(h, leaf, a, false);
				}
			);
			last_chosen_thread = h->chosen_thread;
		}
	} else if (ancestor->all_explored) {
		/* The subtree should not have existed, but we already
		 * finished exploring it, so we can't adjust estimates. */
		return;
	} else {
		/* The subtree was not explored yet, so we need to readjust the
		 * ancestor node's estimate downwards. */
		a->do_explore = false;
		assert(ancestor->marked_children > 1);
		ancestor->marked_children--;

		/* recompute proportions for ancestor and its children along the
		 * branch we came from */
		long double old_proportion = ancestor->proportion;
		adjust_subtree_proportions(ancestor, leaf,
					   ancestor->marked_children + 1,
					   ancestor->marked_children);
		/* recompute subtree time for ancestor alone (not propagated
		 * down). note that num explored children (the denominator)
		 * does not change. while for proportion, marked children is
		 * the denominator, here it is part of the numerator. */
		long double old_usecs = ancestor->subtree_usecs;
		ancestor->subtree_usecs /= ancestor->marked_children + 1;
		ancestor->subtree_usecs *= ancestor->marked_children;

		/* find how much proportion and subtree time changed */
		long double proportion_delta = ancestor->proportion - old_proportion;
		long double subtree_delta = ancestor->subtree_usecs - old_usecs;
		assert(proportion_delta >= 0);
		assert(subtree_delta <= 0);

		/* propagate proportion and subtree time to older ancestors */
		for (struct hax *h = ancestor->parent; h != NULL; h = h->parent) {
			/* proportion is simply added/subtracted from parents */
			h->proportion += proportion_delta;
			/* subtree delta gets factored into the parent's average.
			 * unlike proportion, subtree delta changes at each level. */
			subtree_delta *= h->marked_children;
			subtree_delta /= Q_GET_SIZE(&h->children);
			h->subtree_usecs += subtree_delta;
		}
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

struct human_friendly_time { uint64_t secs, mins, hours, days, years; bool inf; };

static void human_friendly_time(long double usecs, struct human_friendly_time *hft)
{
	long double secs = usecs / 1000000;
	if ((hft->inf = (secs > (long double)UINT64_MAX))) {
		return;
	}

	hft->years = 0;
	hft->days = 0;
	hft->hours = 0;
	hft->mins = 0;
	hft->secs = (uint64_t)secs;
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
	if (hft->inf) {
		printf(v, "INF");
		return;
	}

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
	unsigned int branches = ls->save.total_jumps + 1;

	lsprintf(BRANCH, COLOUR_BOLD COLOUR_GREEN
		 "Estimate: %Lf%% (%Lf total branches)\n" COLOUR_DEFAULT,
		 proportion * 100, (long double)branches / proportion);

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

	uint64_t time_asleep =
		message_estimate(&ls->mess, proportion, branches,
				 usecs, ls->save.total_usecs,
				 ls->sched.icb_preemption_count, ls->icb_bound);
	fudge_time(&ls->save.last_save_time, time_asleep);
}
