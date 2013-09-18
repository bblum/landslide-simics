/**
 * @file schedule.h
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#ifndef __LS_SCHEDULE_H
#define __LS_SCHEDULE_H

#include <simics/api.h>

#include "common.h"
#include "kernel_specifics.h"
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
		/* Threads get a "free pass" out of the context switch exiting
		 * assertion the first time, since some kernels have newly
		 * forked threads not exit through the context switcher, so we
		 * start all threads with the above flag turned off.
		 * This makes the assertion weaker than if we got to use
		 * kern_fork_returns_to_cs, but we can't use it, so I think this
		 * is the strongest we can do besides. */
		bool cs_free_pass;
		/* are they about to create a new thread? */
		bool forking;
		/* about to take a spin on the sleep queue? */
		bool sleeping;
		/* do they have properties of red wizard? */
		bool vanishing;
		/* are they reading lines */
		bool readlining;
		/* have they not even had a chance to run yet? */
		bool just_forked;
		/* are they taking or releasing a mutex? */
		bool mutex_locking;
		bool mutex_unlocking;
		/* are we trying to schedule this agent? */
		bool schedule_target;
	} action;
	/* For noob deadlock detection. The pointer might not be set; if it is
	 * NULL but the tid field is not -1, it should be computed. */
	struct agent *blocked_on;
	int blocked_on_tid;
	/* action.locking implies addr is valid; also blocked_on set implies
	 * locking, which implies addr is valid. -1 if nothing. */
	int blocked_on_addr;
	/* Used by partial order reduction, only in "oldsched"s in the tree. */
	bool do_explore;
};

Q_NEW_HEAD(struct agent_q, struct agent);

#define BLOCKED(a) ((a)->blocked_on_tid != -1)

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
	/* Denotes whether the current thread is not on the runqueue and yet is
	 * runnable anyway. (True only in some kernels.) */
	bool current_extra_runnable;
	/* Counters - useful for telling when tests begin and end */
	int num_agents;
	int most_agents_ever;
	/* See agent_vanish for justification */
	struct agent *last_vanished_agent;
	/* Did the guest finish initialising its own state */
	bool guest_init_done;
	/* It does take many instructions for us to switch, after all. This is
	 * NULL if we're not trying to schedule anybody. */
	struct agent *schedule_in_flight;
	bool delayed_in_flight;
	bool just_finished_reschedule;
	/* Whether we think we ought to be entering the timer handler or not. */
	bool entering_timer;
	/* The stack trace for the most recent voluntary reschedule. Used in
	 * save.c, when voluntary resched decision points are too late to get
	 * a useful stack trace. Set at context switch entry. */
	int voluntary_resched_tid;
	char *voluntary_resched_stack;
	/* TODO: have a scheduler-global schedule_landing to assert against the
	 * per-agent flag (only violated by interrupts we don't control) */
};

#define EVAPORATE_FLOW_CONTROL(code) \
	do {								\
	bool __nested_loop_guard = true;				\
	do {								\
		assert(__nested_loop_guard &&				\
		       "Illegal 'continue' in nested loop macro");	\
		__nested_loop_guard = false;				\
		code;							\
		__nested_loop_guard = true;				\
	} while (!__nested_loop_guard);					\
	assert(__nested_loop_guard &&					\
	       "Illegal 'break' in nested loop macro");			\
	} while (0)

/* Can't be used with 'break' or 'continue', though 'return' is fine. */
#define FOR_EACH_RUNNABLE_AGENT(a, s, code) do {	\
	bool __idle_is_runnable = true;			\
	if ((s)->current_extra_runnable) {		\
		a = (s)->cur_agent;			\
		__idle_is_runnable = false;		\
		EVAPORATE_FLOW_CONTROL(code);		\
	}						\
	Q_FOREACH(a, &(s)->rq, nobe) {			\
		if (kern_has_idle() &&			\
		    a->tid == kern_get_idle_tid())	\
			continue;			\
		__idle_is_runnable = false;		\
		EVAPORATE_FLOW_CONTROL(code);		\
	}						\
	Q_FOREACH(a, &(s)->sq, nobe) {			\
		if (kern_has_idle() &&			\
		    a->tid == kern_get_idle_tid())	\
			continue; /* why it sleep?? */	\
		__idle_is_runnable = false;		\
		EVAPORATE_FLOW_CONTROL(code);		\
	}						\
	if (__idle_is_runnable && kern_has_idle()) {	\
		a = agent_by_tid_or_null(&(s)->dq, kern_get_idle_tid()); \
		if (a == NULL)				\
			a = agent_by_tid_or_null(&(s)->rq, kern_get_idle_tid()); \
		assert(a != NULL && "couldn't find idle in FOR_EACH");	\
		EVAPORATE_FLOW_CONTROL(code);		\
	}						\
	} while (0)

struct agent *agent_by_tid_or_null(struct agent_q *, int);

void sched_init(struct sched_state *);

void print_agent(verbosity v, const struct agent *);
void print_q(verbosity v, const char *, const struct agent_q *, const char *);
void print_qs(verbosity v, const struct sched_state *);

/* called at every "interesting" point ... */
void sched_update(struct ls_state *);
/* called after time-travel */
void sched_recover(struct ls_state *);

#endif
