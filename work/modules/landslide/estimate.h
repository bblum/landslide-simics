/**
 * @file estimate.h
 * @brief online state space size estimation
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#ifndef __LS_ESTIMATE_H
#define __LS_ESTIMATE_H

#include <sys/time.h>

struct hax;

struct marked_history {
	long double marked;
	int samples;
};

struct estimate_state {
	struct marked_history *history;
	unsigned int history_depth; // length of history array

	/* Records the timestamp last time we arrived at a node in the tree.
	 * This is updated only during save_setjmp -- it doesn't need to be during
	 * save_longjmp because each longjmp is immediately after a call to setjmp
	 * on the last nobe in the previous branch. */
	struct timeval last_save_time;
};

void estimate_init(struct estimate_state *e);

uint64_t estimate_update_time(struct estimate_state *e);

void estimate(struct estimate_state *e, struct hax *root, struct hax *current);

#endif
