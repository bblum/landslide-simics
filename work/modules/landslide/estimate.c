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
	lsprintf(DEV, "Tag density history: [");
	if (e->history != NULL) {
		for (int i = 0; i <= e->history_max; i++) {
			printf(DEV, "(%Lf / %Lf * %u)", e->history[i].avg_marked,
			       e->history[i].avg_total, e->history[i].samples);
			if (i < e->history_max) {
				printf(DEV, ", ");
			}
		}
	}
	printf(DEV, "]\n");
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
		assert(h->marked_children >= h->marked_children_old);

		/* p = product_vi 1/(Marked(vi) + F(vi)).
		 * TODO: explore F(vi) != 0, i.e., not-lazy-strategy */
		this_nobe_proportion /= h->marked_children;
		assert(this_nobe_proportion >= 0);

		/* Step 1-2 -- Retroactively fix-up past branch probabilities. */

		lsprintf(DEV, "last %Lf, this %Lf\n", old_proportion, h->proportion);

		/* Stash old proportion value so we can compute the delta.
		 * Also check here the invariant that this nobe's child's old
		 * proportion was less than this one's. */
		assert(old_proportion <= h->proportion);
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

	lsprintf(BRANCH, "Estimate: %Lf\n", root->proportion);
}
