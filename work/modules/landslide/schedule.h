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
	/* Link in our runqueue */
	Q_NEW_LINK(struct agent) nobe;
	/* state tracking for what the corresponding kthread is up to */
	struct {
		/* is there a timer handler frame on this thread's stack?
		 * TODO: similar for keyboard, if it becomes relevant */
		bool handling_timer;
		/* are they about to create a new thread? */
		bool forking;
		/* do they have properties of red wizard? */
		bool vanishing;
	} action;
};

Q_NEW_HEAD(struct agent_q, struct agent);

/* Internal state for the scheduler. */
struct sched_state {
	/* Reflection of the currently runnable threads in the guest kernel */
	struct agent_q rq;
	/* Reflection of threads which exist but are not runnable */
	struct agent_q dq;
	/* Currently active thread */
	struct agent *cur_agent;
	/* Are we about to context-switch? (see inside sched_update) */
	bool context_switch_pending;
	int context_switch_target;
	/* See agent_vanish for justification */
	struct agent *last_vanished_agent;
	/* Did the guest finish initialising its own state */
	bool guest_init_done;
	/* It does take many instructions for us to switch, after all */
	bool schedule_in_progress;
};

void sched_init(struct sched_state *);

/* called at every "interesting" point ... */
// TODO: is this the right interface?
void sched_update(struct ls_state *);

#endif
