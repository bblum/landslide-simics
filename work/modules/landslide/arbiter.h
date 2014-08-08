/**
 * @file arbiter.h
 * @author Ben Blum
 * @brief decision-making routines for landslide
 */

#ifndef __LS_ARBITER_H
#define __LS_ARBITER_H

#include "variable_queue.h"

struct ls_state;
struct agent;

struct choice {
	unsigned int tid;
	Q_NEW_LINK(struct choice) nobe;
};

Q_NEW_HEAD(struct choice_q, struct choice);

struct arbiter_state {
	struct choice_q choices;
};

/* maintenance interface */
void arbiter_init(struct arbiter_state *);
void arbiter_append_choice(struct arbiter_state *, unsigned int tid);
bool arbiter_pop_choice(struct arbiter_state *, unsigned int *tid);

/* scheduling interface */
bool arbiter_interested(struct ls_state *, bool just_finished_reschedule,
			bool *voluntary, bool *need_handle_sleep, bool *data_race);
bool arbiter_choose(struct ls_state *, struct agent *current,
		    struct agent **result, bool *our_choice);

#endif
