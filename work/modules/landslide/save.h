/**
 * @file save.h
 * @brief facility for saving and restoring arbiter choice trees
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_SAVE_H
#define __LS_SAVE_H

#include "schedule.h"

/* saving and restoring uses a directory tree structure on the filesystem to
 * represent the possible choices at each decision point. */

struct save_state {
	/* the current directory - the base directory provided by the init
	 * call, plus each choice that has been made. */
	struct {
		char *path;
		int len;
	} dir;
	/* the most recent uncommitted choice */
	struct {
		int eip;
		int tid;
		bool live;
	} last_choice;
	/* save_choice_commit may once be called before save_choice is called,
	 * to initialise the base directory. this flag is for state tracking. */
	bool save_choice_ever_called;
};

void save_init(struct save_state *);
bool save_set_base_dir(struct save_state *, const char *base_dir);

const char *save_get_path(struct save_state *);

/* recording a choice made is done in two passes - this value is set as soon as
 * a scheduling decision is made, and when the next decision point (or end of
 * test case) is reached, this value is "committed" to the filesystem along
 * with relevant metadata for that choice point (i.e., what tids are available
 * to choose next). */
/* TODO: how will this work with the base case? */
void save_choice(struct save_state *, int eip, int tid);
void save_choice_commit(struct save_state *, struct ls_state *,
			bool our_choice);


#endif
