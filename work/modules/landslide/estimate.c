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
	e->history       = NULL;
	e->history_depth = 0;
	e->history_max   = 0;
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

void estimate_update_history(struct estimate_state *e, unsigned int depth,
			     unsigned int marked, unsigned int total)
{
	if (e->history_depth <= depth) {
		/* Resize history array. */
		unsigned int new_depth = depth * 2;
		struct marked_history *new_history =
			MM_XMALLOC(new_depth, struct marked_history);
		memset(new_history, 0, new_depth * sizeof(struct marked_history));
		memcpy(new_history, e->history,
		       e->history_depth * sizeof(struct marked_history));
		MM_FREE(e->history);
		e->history = new_history;
		e->history_depth = new_depth;
	}
	if (e->history_max < depth) {
		e->history_max = depth;
	}
	/* Recompute new averages. */
	struct marked_history *entry = &e->history[depth];
	entry->avg_marked = ((entry->avg_marked * entry->samples) + marked) /
	                    (entry->samples + 1);
	entry->avg_total  = ((entry->avg_total * entry->samples) + total) /
	                    (entry->samples + 1);
	entry->samples++;
}

void estimate_print_history(struct estimate_state *e)
{
	lsprintf(INFO, "Tag density history: [");
	if (e->history != NULL) {
		for (int i = 0; i <= e->history_max; i++) {
			printf(INFO, "(%Lf / %Lf * %u)", e->history[i].avg_marked,
			       e->history[i].avg_total, e->history[i].samples);
			if (i < e->history_max) {
				printf(INFO, ", ");
			}
		}
	}
	printf(INFO, "]\n");
}

/******************** actual estimation algorithm follows ********************/

//#define HISTORY
#define HISTORY_FUZZ
#define HISTORY_FUZZ_FACTOR 1.0 // dropoff exponent

#ifdef HISTORY

#ifdef HISTORY_FUZZ
static long double figure_out_history(struct estimate_state *e, int depth, int total)
{
	// compute a weighted average, with exponentially-dropping-off factors
	// from history values stored in history entries at other depths.
	long double avg_marked = 0; // total across depths
	long double avg_total  = 0; // total across depths
	long double samples = 0; // denominator

	// Iterate in both directions, until both ends of the array are hit.
	for (int i = 0; depth + i <= e->history_max || depth - i >= 0; i++) {
		if (depth + i <= e->history_max) {
			if (e->history[depth + i].samples > 0) {
				assert(e->history[depth + i].avg_total >= 0);
				assert(e->history[depth + i].avg_total > 0);
				avg_marked += e->history[depth + i].avg_marked;
				avg_total  += e->history[depth + i].avg_total;
				// FIXME: Do we want to also weigh by number of
				// samples in each history entry?
				// TODO tomorrow: try multiplying above two
				// values by entry.samples, and doing here
				// "samples += entry.samples".
				samples++;
			}
		}
		// Extra != 0 clause because for the entry-at-this-depth case we
		// oughtn't repeat work; this would be the same as the above if.
		if (i != 0 && depth - i >= 0) {
			if (e->history[depth - i].samples > 0) {
				assert(e->history[depth - i].avg_total >= 0);
				assert(e->history[depth - i].avg_total > 0);
				avg_marked += e->history[depth - i].avg_marked;
				avg_total  += e->history[depth - i].avg_total;
				// FIXME: As above.
				samples++;
			}
		}

		// Weigh entries +/- depth in either direction by the same
		// fudge amount.
		avg_marked *= HISTORY_FUZZ_FACTOR;
		avg_total  *= HISTORY_FUZZ_FACTOR;
		samples    *= HISTORY_FUZZ_FACTOR;
	}

	if (samples == 0) {
		return 0;
	} else {
		assert(avg_total >= 0);
		assert(avg_total > 0);
		// 'samples' factor in marked/total cancel out.
		return total * avg_marked / avg_total;
	}
}
#else
static long double figure_out_history(struct estimate_state *e, int depth, int total)
{
	struct marked_history *entry = &e->history[depth];
	/* Compute number of expected tagged children based on history's
	 * average fraction of tagged children. */
	assert(entry->avg_marked <= entry->avg_total);
	if (entry->samples == 0) {
		return 0;
	} else {
		assert(entry->avg_total != 0);
		long double result = entry->avg_marked * total / entry->avg_total;
		// "assert(result <= total)", corrected for possible FP error.
		if (result > total) {
			assert(result < total + 0.00001);
			result = total;
		}
		return result;
	}
}
#endif

/* Updates a nobe's marked_children field based on past history. */
static void adjust_for_history(struct estimate_state *e, struct hax *h, int total)
{
	/* Can only ask history for advice if it has advice to give. */
	if (h->depth <= e->history_max) {
		long double history_says = figure_out_history(e, h->depth, total);

		/* Strategy 1: Replace it entirely.*/
		// TODO: Strategy 2: Use the average. Research question: Find a
		// heuristic value for weighted average that works well.
		if (h->marked_children < history_says) {
			lsprintf(DEV, "At depth %d, %Lf/%d were marked, but "
				 "history says %Lf\n", h->depth,
				 h->marked_children, total, history_says);
			// Cap maximum possible value, obviously.
			if (history_says < total) {
				h->marked_children = history_says;
			} else {
				h->marked_children = total;
			}
		}
	}
	return;
}
#endif

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

void estimate(struct estimate_state *e, struct hax *root, struct hax *current)
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
#ifndef HISTORY
		/* No adjustment for history (F(vi) = 0). */
		assert(h->marked_children >= h->marked_children_old);
#else
		/* Adjust marked children value heuristically with history */
		adjust_for_history(e, h, total_children);
#endif

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
#ifndef HISTORY
			// NB. With history heuristic, this assert does not
			// necessarily hold. We could adjust downwards.
			assert(h->marked_children > h->marked_children_old);
#endif
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

	lsprintf(BRANCH, "Estimate: %Lf\n", root->proportion);
}
