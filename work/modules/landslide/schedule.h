/**
 * @file schedule.h
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#ifndef __LS_SCHEDULE_H
#define __LS_SCHEDULE_H

#include "variable_queue.h"

struct ls_state;

/* The agent represents a single thread, or active schedulable node on the
 * runqueue. */
struct agent {
	int tid;
	Q_NEW_LINK(struct agent) nobe;
};

Q_NEW_HEAD(agent_rq_t, struct agent);

/* Internal state for the scheduler. */
struct sched_state {
	/* Reflection of the currently runnable threads in the guest kernel */
	agent_rq_t rq;
	/* Currently active thread */
	int current_thread;
	/* It does take many instructions for us to switch, after all */
	bool schedule_in_progress;
};

void sched_init(struct sched_state *);

/* called at every "interesting" point ... */
// TODO: is this the right interface?
void sched_update(struct ls_state *);

#endif
