/**
 * @file save.h
 * @brief save/restore facility amidst the choice tree
 * @author Ben Blum
 */

#ifndef __LS_SAVE_H
#define __LS_SAVE_H

struct ls_state;
struct hax;

// TODO: examine the attributes!

struct save_state {
	/* The root of the decision tree, or NULL if save_setjmp() was never
	 * called. */
	struct hax *root;
	/* If root is set, this points to the "current" node in the tree */
	struct hax *current;
	int next_tid;
	bool just_jumped;
};

void save_init(struct save_state *);

/* Current state, and the next_tid/our_choice is about the next in-flight
 * choice. */
void save_setjmp(struct save_state *, struct ls_state *,
		 int next_tid, bool our_choice);

/* If hax is NULL, then longjmps to the root. Otherwise, hax must be between
 * the current choice point and the root (inclusive). */
void save_longjmp(struct save_state *, struct ls_state *, struct hax *);

#endif
