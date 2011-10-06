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

	Q_FOREACH(a, &h->oldsched->rq, nobe) {
		if (is_child_searched(h, a->tid)) {
			continue;
		} else {
			*new_tid = a->tid;
			return true;
		}
	}
	Q_FOREACH(a, &h->oldsched->sq, nobe) {
		if (is_child_searched(h, a->tid)) {
			continue;
		} else {
			*new_tid = a->tid;
			return true;
		}
	}

	return false;
}

struct hax *explore(struct hax *root, struct hax *current, int *new_tid)
{
	struct hax *our_branch = NULL;
	struct hax *rabbit = current;

	assert(root != NULL);
	assert(current != NULL);

	/* Find the most recent spot in our branch that is not all explored. */
	while (1) {
		assert(current->oldsched != NULL);

		/* Examine children */
		if (!current->all_explored) {
			if (find_unsearched_child(current, new_tid)) {
				lsprintf("chose tid %d from tid %d (%p)\n",
					 *new_tid, current->chosen_thread,
					 current);
				return current;
			} else {
				lsprintf("tid %d (%p) all_explored\n",
					 current->chosen_thread, current);
				current->all_explored = true;
			}
		}

		/* our_branch chases, indicating where we came from */
		our_branch = current;
		/* 'current' finds the most recent unexplored */
		if ((current = current->parent) == NULL) {
			assert(our_branch == root && "two roots?!?");
			lsprintf("root of tree all_explored!\n");
			return NULL;
		}
		/* cycle check */
		if (rabbit) rabbit = rabbit->parent;
		if (rabbit) rabbit = rabbit->parent;
		assert(rabbit != current && "tree has cycle??");
	}
}

