/**
 * @file schedule.h
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#ifndef __LS_SCHEDULE_H
#define __LS_SCHEDULE_H

#include <simics/api.h>

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
		 * FIXME: similar for keyboard will be needed */
		bool handling_timer;
		/* are they in the context switcher? (note: this and
		 * handling_$INTERRUPT are not necessarily linked!) */
		bool context_switch;
		/* are they about to create a new thread? */
		bool forking;
		/* about to take a spin on the sleep queue? */
		bool sleeping;
		/* do they have properties of red wizard? */
		bool vanishing;
		/* are they reading lines */
		bool readlining;
		/* are we trying to schedule this agent? */
		bool schedule_target;
	} action;
	/* For noob deadlock detection. The pointer might not be set; if it is
	 * NULL but the tid field is not -1, it should be computed. */
	struct agent *blocked_on;
	int blocked_on_tid;
};

Q_NEW_HEAD(struct agent_q, struct agent);

/* Internal state for the scheduler.
 * If you change this, make sure to update save.c! */
struct sched_state {
	/* Reflection of the currently runnable threads in the guest kernel */
	struct agent_q rq;
	/* Reflection of threads which exist but are not runnable */
	struct agent_q dq;
	/* Reflection of threads which will become runnable on their own time */
	struct agent_q sq;
	/* Currently active thread */
	struct agent *cur_agent;
	struct agent *last_agent;
	/* Are we about to context-switch? (see inside sched_update) */
	bool context_switch_pending; /* valid flag for target below */
	int context_switch_target;
	/* See agent_vanish for justification */
	struct agent *last_vanished_agent;
	/* Did the guest finish initialising its own state */
	bool guest_init_done;
	/* It does take many instructions for us to switch, after all. This is
	 * NULL if we're not trying to schedule anybody. */
	struct agent *schedule_in_flight;
	bool entering_timer; /* only for debugging/asserting purposes */
	/* TODO: have a scheduler-global schedule_landing to assert against the
	 * per-agent flag (only violated by interrupts we don't control) */
};

struct agent *agent_by_tid_or_null(struct agent_q *, int);
struct agent *agent_by_tid(struct agent_q *, int);

void sched_init(struct sched_state *);

void print_agent(struct agent *);
void print_q(const char *, struct agent_q *, const char *);
void print_qs(struct sched_state *);

/* called at every "interesting" point ... */
void sched_update(struct ls_state *);
/* called after time-travel */
void sched_recover(struct ls_state *);

#endif
