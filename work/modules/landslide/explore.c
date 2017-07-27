/**
 * @file explore.c
 * @brief DPOR
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define MODULE_NAME "EXPLORE"
#define MODULE_COLOUR COLOUR_BLUE

#include "common.h"
#include "estimate.h"
#include "landslide.h"
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
		/* If the root PP is a non-enabled speculative DR, we can get
		 * a bit stuck in a corner if we need to tag a child. FIXME:
		 * Really what we want is the oldest non-speculative ancestor,
		 * and to change the condition at the fixme in explore() to see
		 * if ancestor has no non-speculative parent. */
		if (h->parent == NULL) {
			h->is_preemption_point = true;
		}
	} while (!h->is_preemption_point);
	return h;
}

static bool tag_good_sibling(struct hax *h0, struct hax *ancestor,
			     unsigned int icb_bound, bool *need_bpor)
{
	unsigned int tid = h0->chosen_thread;
	struct hax *grandparent = pp_parent(ancestor);
	assert(need_bpor == NULL || !*need_bpor);

	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, grandparent->oldsched,
		if (a->tid == tid) {
			if (BLOCKED(a) || HTM_BLOCKED(grandparent->oldsched, a) ||
			    is_child_searched(grandparent, a->tid)) {
				return false;
			} else if (ICB_BLOCKED(grandparent->oldsched, icb_bound,
					       grandparent->voluntary, a)) {
				if (need_bpor != NULL) {
					*need_bpor = true;
					lsprintf(DEV, "from #%d/tid%d, want TID "
						 "%d, sibling of #%d/tid%d, but "
						 "ICB says no :(\n", h0->depth,
						 h0->chosen_thread, a->tid,
						 ancestor->depth,
						 ancestor->chosen_thread);
				}
				return false;
			} else {
				/* normal case; thread can be tagged */
				a->do_explore = true;
				lsprintf(DEV, "from #%d/tid%d, tagged TID %d%s, "
					 "sibling of #%d/tid%d\n", h0->depth,
					 h0->chosen_thread, a->tid,
					 need_bpor == NULL ? " (during BPOR)" : "",
					 ancestor->depth, ancestor->chosen_thread);
				return true;
			}
		}
	);

	return false;
}

static void tag_all_siblings(struct hax *h0, struct hax *ancestor,
			     unsigned int icb_bound, bool *need_bpor)
{
	struct hax *grandparent = pp_parent(ancestor);
	unsigned int num_tagged = 0;

	lsprintf(DEV, "from #%d/tid%d%s, tagged all siblings of #%d/tid%d: ",
		 h0->depth, h0->chosen_thread,
		 need_bpor == NULL ? " (during BPOR)" : "",
		 ancestor->depth, ancestor->chosen_thread);

	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, grandparent->oldsched,
		if (BLOCKED(a) || HTM_BLOCKED(grandparent->oldsched, a) ||
		    is_child_searched(grandparent, a->tid)) {
			// continue;
		} else if (ICB_BLOCKED(grandparent->oldsched, icb_bound,
				       grandparent->voluntary, a)) {
			/* also continue, but note that this sibling will
			 * require a BPOR backtrack (though not all ones might).
			 * this is a site for potential reduction -- if another
			 * not-icb-blocked sibling is tagged, and successfully
			 * leads to the desired thread reordering, that's enough
			 * to know to untag something from this case. */
			if (need_bpor != NULL) {
				*need_bpor = true;
				printf(DEV, "(tid%d needs BPOR) ", a->tid);
			}
		} else {
			/* normal case; sibling can be tagged */
			a->do_explore = true;
			print_agent(DEV, a);
			printf(DEV, " ");
			num_tagged++;
		}
	);

	printf(DEV, "\n");

	assert(need_bpor == NULL || !*need_bpor || num_tagged <= 2);
}

static bool tag_sibling(struct hax *h0, struct hax *ancestor,
			unsigned int icb_bound)
{
	bool need_bpor = false;
	if (!tag_good_sibling(h0, ancestor, icb_bound, &need_bpor)) {
		tag_all_siblings(h0, ancestor, icb_bound, &need_bpor);
	}
	return need_bpor;
}

#ifdef ICB
static bool stop_bpor_backtracking(struct hax *h0, struct hax *ancestor2)
{
	/* Don't BPOR-tag past the previous transition of same thread... */
	if (h0->chosen_thread == ancestor2->chosen_thread) {
		return true;
	/* ...or past the point where the thread was created. */
	} else if (find_agent(ancestor2->oldsched, h0->chosen_thread) == NULL) {
		return true;
	} else {
		return false;
	}

}

/* BPOR [Coons et al, OOPSLA 2013]. */
static void tag_reachable_aunts(struct hax *h0, struct hax *ancestor,
				unsigned int icb_bound)
{
	bool good_sibling_tagged = false;
	assert(ancestor != NULL);

	lsprintf(DEV, "BPOR trying to reorder #%d/tid%d around E.A. #%d/tid%d\n",
		 h0->depth, h0->chosen_thread,
		 ancestor->depth, ancestor->chosen_thread);

	/* Search among ancestors for a place where we could switch to running
	 * h0's thread without exceeding the preemption bound. */
	for (struct hax *ancestor2 = ancestor->parent;
	     ancestor2 != NULL && ancestor2->parent != NULL
	     && !stop_bpor_backtracking(h0, ancestor2);
	     ancestor2 = ancestor2->parent) {
		/* May need to tag multiple times for same reason as not
		 * using "break" in the main dpor loop below. */
		if (tag_good_sibling(h0, ancestor2, icb_bound, NULL)) {
			lsprintf(DEV, "BPOR can run #%d/tid%d after reachable "
				 "aunt #%d/tid%d\n", h0->depth, h0->chosen_thread,
				 ancestor2->depth, ancestor2->chosen_thread);
			good_sibling_tagged = true;
		}
	}

	if (good_sibling_tagged) {
		return;
	} else {
		lsprintf(DEV, "BPOR, ouch! lots of conservative tags incoming\n");
	}

	/* If got here, couldn't find a way to run h0's thread directly. But
	 * there may be another way to make it runnable using all_siblings
	 * which is reachable within the bound (either because the other sibling
	 * is runnable during a voluntary resched, or because the preemption
	 * count changes among these ancestors). */
	for (struct hax *ancestor2 = ancestor->parent;
	     ancestor2 != NULL && ancestor2->parent != NULL
	     && !stop_bpor_backtracking(h0, ancestor2);
	     ancestor2 = ancestor2->parent) {
		tag_all_siblings(h0, ancestor2, icb_bound, NULL);
	}
}
#endif

static bool any_tagged_child(struct hax *h, unsigned int *new_tid, bool *txn,
			     unsigned int *xabort_code)
{
	struct agent *a;

	if ((*txn = (h->xbegin && ARRAY_LIST_SIZE(&h->xabort_codes_todo) > 0))) {
		// TODO: reduction challenge?
		/* pop the code from the list so it won't be doubly explored */
		*xabort_code = *ARRAY_LIST_GET(&h->xabort_codes_todo, 0);
		ARRAY_LIST_REMOVE_SWAP(&h->xabort_codes_todo, 0);
		/* injecting a failure in the xbeginning thread, obvs. */
		*new_tid = h->chosen_thread;
		return true;
	}

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

struct hax *explore(struct ls_state *ls, unsigned int *new_tid, bool *txn,
		    unsigned int *xabort_code)
{
	struct save_state *ss = &ls->save;
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
			// FIXME: see fixme in pp_parent
			if (ancestor->parent == NULL) {
				continue;
			/* ...however, in this inner loop, we never consider
			 * user yield-blocked threads as potential transitions
			 * to interleave around. */
			} else if (is_user_yield_blocked(ancestor)) {
				if (h->chosen_thread != ancestor->chosen_thread) {
					lsprintf(INFO, "not comparing #%d/tid%d "
						 "to #%d/tid%d; the latter is "
						 "yield-blocked\n", h->depth,
						 h->chosen_thread, ancestor->depth,
						 ancestor->chosen_thread);
				}
				continue;
			} else if (!is_evil_ancestor(h, ancestor)) {
				continue;
			}

			/* The ancestor is "evil". Find which siblings need to
			 * be explored. */
			bool need_bpor = tag_sibling(h, ancestor, ls->icb_bound);
#ifdef ICB
			if (need_bpor) {
				tag_reachable_aunts(h, ancestor, ls->icb_bound);
				ls->icb_need_increment_bound = true;
			}
#else
			assert(!need_bpor);
#endif

			/* In theory, stopping after the first baddie
			 * is fine; the others would be handled "by
			 * induction". But that relies on choice points
			 * being comprehensive enough, which we almost
			 * always do not satisfy. So continue. (See MS
			 * thesis section 5.4.3 / figure 5.4.) */
			/* break; */
		}
	}

	/* We will choose a tagged sibling that's deepest, to maintain a
	 * depth-first ordering. This allows us to avoid having tagged siblings
	 * outside of the current branch of the tree. A trail of "all_explored"
	 * flags gets left behind. */
	for (struct hax *h = current->parent; h != NULL; h = h->parent) {
		if (any_tagged_child(h, new_tid, txn, xabort_code)) {
			assert(h->is_preemption_point);
			lsprintf(BRANCH, "from #%d/tid%d, chose tid %d%s, "
				 "child of #%d/tid%d\n", current->depth,
				 current->chosen_thread, *new_tid,
				 *txn ? " (xbegin failure injection)" : "",
				 h->depth, h->chosen_thread);
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
