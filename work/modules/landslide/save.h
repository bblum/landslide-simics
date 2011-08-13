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
	bool started;
};

void save_init(struct save_state *, char *base_dir);
void save_append_tid(struct save_state *, int);
void save_start_here(struct save_state *, struct ls_state *);

/* recording a choice made is done in two passes - this value is set as soon as
 * a scheduling decision is made, and when the next decision point (or end of
 * test case) is reached, this value is "committed" to the filesystem along
 * with relevant metadata for that choice point (i.e., what tids are available
 * to choose next). */
/* TODO: how will this work with the base case? */
void save_choice(struct save_state *, int eip, int tid);
void save_choice_commit(struct save_state *, struct ls_state *);


#endif
