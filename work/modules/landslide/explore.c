/**
 * @file explore.c
 * @brief DPOR
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define MODULE_NAME "EXPLORE"
#define MODULE_COLOUR COLOUR_BLUE

#include "common.h"
#include "estimate.h"
#include "save.h"
#include "schedule.h"
#include "tree.h"
#include "user_sync.h"
#include "variable_queue.h"

static bool is_child_searched(struct hax *h, unsigned int child_tid) {
	struct hax *child;

	Q_FOREACH(child, &h->children, sibling) {
		if (child->chosen_thread == child_tid && child->all_explored)
			return true;
	}
	return false;
}

static void branch_sanity(struct hax *root, struct hax *current)
{
	struct hax *our_branch = NULL;
	struct hax *rabbit = current;

	assert(root != NULL);
	assert(current != NULL);

	while (1) {
		assert(current->oldsched != NULL);
		/* our_branch chases, indicating where we came from */
		our_branch = current;
		if ((current = current->parent) == NULL) {
			assert(our_branch == root && "two roots?!?");
			return;
		}
		/* cycle check */
		if (rabbit) rabbit = rabbit->parent;
		if (rabbit) rabbit = rabbit->parent;
		assert(rabbit != current && "tree has cycle??");
	}
}

static bool is_evil_ancestor(struct hax *h0, struct hax *h)
{
	return !h0->happens_before[h->depth] && h0->conflicts[h->depth];
}

/* Finds the nearest parent save point that's actually a preemption point.
 * This is how we skip over speculative data race save points when identifying
 * which "good sibling" transitions to tag (when to preempt to get to them?) */
static struct hax *pp_parent(struct hax *h)
{
	do {
		h = h->parent;
		assert(h != NULL);
	} while (!h->is_preemption_point);
	return h;
}

static bool tag_good_sibling(struct hax *h0, struct hax *ancestor)
{
	unsigned int tid = h0->chosen_thread;
	struct hax *grandparent = pp_parent(ancestor);

	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, grandparent->oldsched,
		if (a->tid == tid) {
			if (!BLOCKED(a) &&
			    !is_child_searched(grandparent, a->tid)) {
				a->do_explore = true;
				lsprintf(DEV, "from #%d/tid%d, tagged TID %d, "
					 "sibling of #%d/tid%d\n", h0->depth,
					 h0->chosen_thread, a->tid,
					 ancestor->depth, ancestor->chosen_thread);
				return true;
			} else {
				return false;
			}
		}
	);

	return false;
}

static void tag_all_siblings(struct hax *h0, struct hax *ancestor)
{
	struct hax *grandparent = pp_parent(ancestor);

	lsprintf(DEV, "from #%d/tid%d, tagged all siblings of #%d/tid%d: ",
		 h0->depth, h0->chosen_thread, ancestor->depth,
		 ancestor->chosen_thread);

	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, grandparent->oldsched,
		if (!BLOCKED(a) && !is_child_searched(grandparent, a->tid)) {
			a->do_explore = true;
			print_agent(DEV, a);
			printf(DEV, " ");
		}
	);

	printf(DEV, "\n");
}

static bool any_tagged_child(struct hax *h, unsigned int *new_tid)
{
	struct agent *a;

	/* do_explore doesn't get set on blocked threads, but might get set
	 * on threads we've already looked at. */
	FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
		if (a->do_explore && !is_child_searched(h, a->tid)) {
			*new_tid = a->tid;
			return true;
		}
	);

	return false;
}

static void print_pruned_children(struct save_state *ss, struct hax *h)
{
	bool any_pruned = false;
	struct agent *a;

	FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
		if (!is_child_searched(h, a->tid)) {
			if (!any_pruned) {
				lsprintf(DEV, "at #%d/tid%d pruned tids ",
					 h->depth, h->chosen_thread);
			}
			printf(DEV, "%d ", a->tid);
			any_pruned = true;
		}
	);
	if (any_pruned)
		printf(DEV, "\n");
}

struct hax *explore(struct save_state *ss, unsigned int *new_tid)
{
	struct hax *current = ss->current;

	current->all_explored = true;
	branch_sanity(ss->root, ss->current);

	/* this cannot happen in-line with walking the branch, below, since it
	 * needs to be computed for all ancestors and be ready for checking
	 * against descendants in advance. */
	update_user_yield_blocked_transitions(current);

	/* Compare each transition along this branch against each of its
	 * ancestors. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
		/* In outer loop, we include user threads blocked in a yield
		 * loop as the "descendant" for comparison, because we want
		 * to reorder them before conflicting ancestors if needed... */
		for (struct hax *ancestor = h->parent; ancestor != NULL;
		     ancestor = ancestor->parent) {
			if (ancestor->parent == NULL) {
				continue;
			/* ...however, in this inner loop, we never consider
			 * user yield-blocked threads as potential transitions
			 * to interleave around. */
			} else if (is_user_yield_blocked(ancestor)) {
				if (h->chosen_thread != ancestor->chosen_thread) {
					lsprintf(DEV, "not comparing #%d/tid%d "
						 "to #%d/tid%d; the latter is "
						 "yield-blocked\n", h->depth,
						 h->chosen_thread, ancestor->depth,
						 ancestor->chosen_thread);
				}
				continue;
			/* Is the ancestor "evil"? */
			} else if (is_evil_ancestor(h, ancestor)) {
				/* Find which siblings need to be explored. */
				if (!tag_good_sibling(h, ancestor))
					tag_all_siblings(h, ancestor);

				/* In theory, stopping after the first baddie
				 * is fine; the others would be handled "by
				 * induction". But that relies on choice points
				 * being comprehensive enough, which we almost
				 * always do not satisfy. So continue. (See MS
				 * thesis section 5.4.3 / figure 5.4.) */
				/* break; */
			}
		}
	}

	/* We will choose a tagged sibling that's deepest, to maintain a
	 * depth-first ordering. This allows us to avoid having tagged siblings
	 * outside of the current branch of the tree. A trail of "all_explored"
	 * flags gets left behind. */
	for (struct hax *h = current->parent; h != NULL; h = h->parent) {
		if (any_tagged_child(h, new_tid)) {
			assert(h->is_preemption_point);
			lsprintf(BRANCH, "from #%d/tid%d, chose tid %d, "
				 "child of #%d/tid%d\n",
				 current->depth, current->chosen_thread,
				 *new_tid, h->depth, h->chosen_thread);
			return h;
		} else {
			if (h->is_preemption_point) {
				lsprintf(DEV, "#%d/tid%d all_explored\n",
					 h->depth, h->chosen_thread);
				print_pruned_children(ss, h);
			} else {
				lsprintf(INFO, "#%d/tid%d not a PP\n",
					 h->depth, h->chosen_thread);
			}
			h->all_explored = true;
		}
	}

	lsprintf(ALWAYS, "found no tagged siblings on current branch!\n");
	return NULL;
}
