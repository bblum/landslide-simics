/**
 * @file explore.c
 * @brief choice tree exploration
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#define MODULE_NAME "EXPLORE"
#define MODULE_COLOUR COLOUR_BLUE

#include "common.h"
#include "schedule.h"
#include "tree.h"
#include "variable_queue.h"

static bool is_child_searched(struct hax *h, int child_tid) {
	struct hax *child;

	Q_FOREACH(child, &h->children, sibling) {
		if (child->chosen_thread == child_tid && child->all_explored)
			return true;
	}
	return false;
}

static bool find_unsearched_child(struct hax *h, int *new_tid) {
	struct agent *a;

	FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
		if (!is_child_searched(h, a->tid)) {
			*new_tid = a->tid;
			return true;
		}
	);

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

/******************************************************************************
 * Simple, comprehensive, depth-first exploration strategy.
 ******************************************************************************/

static MAYBE_UNUSED struct hax *simple(struct hax *root, struct hax *current,
				       int *new_tid)
{
	/* Find the most recent spot in our branch that is not all explored. */
	while (1) {
		/* Examine children */
		if (!current->all_explored) {
			if (find_unsearched_child(current, new_tid)) {
				lsprintf(BRANCH, "chose tid %d from tid %d\n",
					 *new_tid, current->chosen_thread);
				return current;
			} else {
				lsprintf(BRANCH, "tid %d all_explored\n",
					 current->chosen_thread);
				current->all_explored = true;
			}
		}

		/* 'current' finds the most recent unexplored */
		if ((current = current->parent) == NULL) {
			lsprintf(ALWAYS, "root of tree all_explored!\n");
			return NULL;
		}
	}
}

/******************************************************************************
 * Dynamic partial-order reduction
 ******************************************************************************/

static bool is_evil_ancestor(struct hax *h0, struct hax *h)
{
	return !h0->happens_before[h->depth] && h0->conflicts[h->depth];
}

static bool tag_good_sibling(struct hax *h0, struct hax *h)
{
	int tid = h0->chosen_thread;

	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, h->parent->oldsched,
		if (a->tid == tid) {
			if (!BLOCKED(a) &&
			    !is_child_searched(h->parent, a->tid)) {
				a->do_explore = true;
				lsprintf(DEV, "from #%d/tid%d, tagged TID %d, "
					 "sibling of #%d/tid%d\n", h0->depth,
					 h0->chosen_thread, a->tid, h->depth,
					 h->chosen_thread);
				return true;
			} else {
				return false;
			}
		}
	);

	return false;
}

static void tag_all_siblings(struct hax *h0, struct hax *h)
{
	struct agent *a;
	lsprintf(DEV, "from #%d/tid%d, tagged all siblings of #%d/tid%d: ",
		 h0->depth, h0->chosen_thread, h->depth, h->chosen_thread);

	FOR_EACH_RUNNABLE_AGENT(a, h->oldsched,
		if (!BLOCKED(a) && !is_child_searched(h->parent, a->tid)) {
			a->do_explore = true;
			print_agent(DEV, a);
			printf(DEV, " ");
		}
	);

	printf(DEV, "\n");
}

static bool any_tagged_child(struct hax *h, int *new_tid)
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

static void print_pruned_children(struct hax *h)
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

static MAYBE_UNUSED struct hax *dpor(struct hax *root, struct hax *current,
				     int *new_tid)
{
	current->all_explored = true;

	/* Compare each transition along this branch against each of its
	 * ancestors. */
	for (struct hax *h = current; h != NULL; h = h->parent) {
		for (struct hax *ancestor = h->parent; ancestor != NULL;
		     ancestor = ancestor->parent) {
			if (ancestor->parent == NULL) {
				// ????
			/* Is the ancestor "evil"? */
			} else if (is_evil_ancestor(h, ancestor)) {
				/* Find which siblings need to be explored. */
				if (!tag_good_sibling(h, ancestor))
					tag_all_siblings(h, ancestor);

				/* In theory, stopping after the first baddie
				 * is fine; the others would be handled "by
				 * induction". But that relies on choice points
				 * being comprehensive enough, which we almost
				 * always do not satisfy. So continue. */
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
			lsprintf(BRANCH, "from #%d/tid%d (%p), chose tid %d, "
				 "child of #%d/tid%d (%p)\n",
				 current->depth, current->chosen_thread, current,
				 *new_tid, h->depth, h->chosen_thread, h);
			return h;
		} else {
			lsprintf(DEV, "#%d/tid%d (%p) all_explored\n",
				 h->depth, h->chosen_thread, h);
			print_pruned_children(h);
			h->all_explored = true;
		}
	}

	lsprintf(ALWAYS, "found no tagged siblings on current branch!\n");
	return NULL;
}

struct hax *explore(struct hax *root, struct hax *current, int *new_tid)
{
	branch_sanity(root, current);
	return dpor(root, current, new_tid);
}
