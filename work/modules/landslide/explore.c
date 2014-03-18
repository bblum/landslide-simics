/**
 * @file explore.c
 * @brief choice tree exploration
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#define MODULE_NAME "EXPLORE"
#define MODULE_COLOUR COLOUR_BLUE

#include "common.h"
#include "estimate.h"
#include "save.h"
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
	assert(0 && "deprecated");
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
 * Skipping over transitions blocked in an open-coded userspace yield loop.
 ******************************************************************************/

/* The approach to deriving if a user thread is "blocked" in an
 * open-coded yield loop involves waiting for it to do a certain number
 * of yields in a row. However, this pollutes the tree with extra
 * branches that are not actually progress, but just that thread ticking
 * up its yield-loop counter. If we treated these as real transitions,
 * we could end up tagging a sibling of every single one of them,
 * while really (assuming we are right that the yield loop is doing
 * nothing) it's a safe reduction technique to tag siblings of only one. */

/* May return NULL if the chosen agent vanished during the transition. */
// FIXME: move this to sched.c
static struct agent *runnable_agent_by_tid(struct sched_state *s, int tid)
{
	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, s,
		if (a->tid == tid) {
			return a;
		}
	);
	return NULL;
}

static void update_user_yield_blocked_transitions(struct hax *h0)
{
	/* Here we scan backwards looking for transitions where we realized
	 * a user thread was yield-blocked (its yield-loop counter hit max),
	 * and adjust the values of that thread's past transitions (where it
	 * was still counting up to the max) to reflect that those transitions
	 * are actually blocked as well.
	 *
	 * One important detail is that we leave the 1st yield-blocked
	 * transition marked unblocked (i.e., don't propagate the max counter
	 * value to it). This allows us to reorder other thread transitions
	 * to occur directly before it.
	 *
	 * While we're at it, of course, we also check the invariant that the
	 * yield-loop counters are either zero or counting up to the max. */

	for (struct hax *h = h0; h->parent != NULL; h = h->parent) {
		struct agent *a = runnable_agent_by_tid(h->oldsched, h->chosen_thread);
		if (a == NULL) {
			/* The chosen thread vanished during its transition.
			 * Definitely not a yield-loop-blocked one. */
			continue;
		}

		assert(a->user_yield_loop_count >= 0);
		if (a->user_yield_loop_count < TOO_MANY_YIELDS) {
			/* Only perform the below (expensive) check when a
			 * thread actually became yield-loop-blocked. */
			continue;
		} else if (a->user_yield_blocked) {
			/* skip it if we already marked it as propagated. */
			lsprintf(DEV, "not propagating YB for #%d/tid%d "
				 "(already did)\n", h->depth, h->chosen_thread);
			continue;
		}
		int expected_count = a->user_yield_loop_count;

		lsprintf(DEV, "scanning ylc for #%d/tid%d, ylc after was %d\n",
			 h->depth, h->chosen_thread, a->user_yield_loop_count);

		/* Propagate backwards in time the fact that this sequence of
		 * yields reached the max number and is considered blocked.
		 * Start at h, not h->parent, to include the one we just saw. */
		for (struct hax *h2 = h; expected_count > 0; h2 = h2->parent) {
			assert(h2 != NULL && "nonzero yield count at root");

			/* Find the past version of the yield-blocked thread. */
			struct agent *a2 = runnable_agent_by_tid(h2->oldsched, a->tid);
			assert(a2 != NULL && "yielding thread vanished in the past");

			/* Its count must have incremented between the end of
			 * this (old) transition and the more recent one. */
			assert(a2->user_yield_loop_count == expected_count);

			/* Skip marking the "first" yield-blocked transition. */
			if (expected_count > 1) {
				lsprintf(DEV,  "#%d/tid%d is YLB (count %d), "
					 "from #%d/tid%d\n",
					 h2->depth, h2->chosen_thread,
					 a2->user_yield_loop_count,
					 h->depth, h->chosen_thread);
				a2->user_yield_blocked = true;
			}

			/* Also, if that thread actually ran during this (old)
			 * transition, expect its count to have increased. */
			struct agent *a3 =
				runnable_agent_by_tid(h2->parent->oldsched, a->tid);
			if (h2->chosen_thread == a->tid) {
				/* Thread ran. Count must have incremented. */
				assert(a2->user_yield_loop_count ==
				       a3->user_yield_loop_count + 1);
				/* Update expected "future" count from the past. */
				expected_count--;
			} else {
				/* Thread did not run; count should not have changed. */
				assert(a2->user_yield_loop_count ==
				       a3->user_yield_loop_count);
			}
		}
	}
}

static bool is_user_yield_blocked(struct hax *h)
{
	struct agent *a = runnable_agent_by_tid(h->oldsched, h->chosen_thread);
	return a != NULL && a->user_yield_blocked;
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

static MAYBE_UNUSED struct hax *dpor(struct save_state *ss, int *new_tid)
{
	struct hax *current = ss->current;

	current->all_explored = true;

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
			print_pruned_children(ss, h);
			h->all_explored = true;
		}
	}

	lsprintf(ALWAYS, "found no tagged siblings on current branch!\n");
	return NULL;
}

struct hax *explore(struct save_state *ss, int *new_tid)
{
	branch_sanity(ss->root, ss->current);
	return dpor(ss, new_tid);
}
