/**
 * @file save.h
 * @brief save/restore facility amidst the choice tree
 * @author Ben Blum
 */

#ifndef __LS_SAVE_H
#define __LS_SAVE_H

#include "estimate.h"

#include <sys/time.h>

struct ls_state;
struct hax;

struct save_state {
	/* The root of the decision tree, or NULL if save_setjmp() was never
	 * called. */
	struct hax *root;
	/* If root is set, this points to the "current" node in the tree */
	struct hax *current;
	int next_tid;
	/* Statistics */
	uint64_t total_choice_poince;
	uint64_t total_choices;
	uint64_t total_jumps;
	uint64_t total_triggers;
	uint64_t depth_total;

	/* Records the timestamp last time we arrived at a node in the tree.
	 * This is updated only during save_setjmp -- it doesn't need to be during
	 * save_longjmp because each longjmp is immediately after a call to setjmp
	 * on the last nobe in the previous branch. */
	struct timeval last_save_time;
	uint64_t total_usecs;
};

void abort_transaction(unsigned int tid, struct hax *h2, unsigned int code);

void save_init(struct save_state *);

void save_recover(struct save_state *, struct ls_state *, int new_tid);

/* Current state, and the next_tid/our_choice is about the next in-flight
 * choice. */
void save_setjmp(struct save_state *, struct ls_state *,
		 int next_tid, bool our_choice, bool end_of_test,
		 bool is_preemption_point, unsigned int data_race_eip,
		 bool voluntary, bool xbegin);

/* If hax is NULL, then longjmps to the root. Otherwise, hax must be between
 * the current choice point and the root (inclusive). */
void save_longjmp(struct save_state *, struct ls_state *, struct hax *);

void save_reset_tree(struct save_state *ss, struct ls_state *ls);

#endif
