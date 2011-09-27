/**
 * @file schedule.c
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#include <simics/api.h>
#include <simics/alloc.h>

#define MODULE_NAME "SCHEDULE"
#define MODULE_COLOUR COLOUR_GREEN

#include "arbiter.h"
#include "common.h"
#include "landslide.h"
#include "kernel_specifics.h"
#include "schedule.h"
#include "variable_queue.h"
#include "x86.h"

/******************************************************************************
 * Agence
 ******************************************************************************/

struct agent *agent_by_tid_or_null(struct agent_q *q, int tid)
{
	struct agent *a;
	Q_SEARCH(a, q, nobe, a->tid == tid);
	return a;
}

struct agent *agent_by_tid(struct agent_q *q, int tid)
{
	struct agent *a = agent_by_tid_or_null(q, tid);
	assert(a && "attempt to look up tid not in specified queue");
	return a;
}

/* Call with whether or not the thread is created with a context-switch frame
 * crafted on its stack. Most threads would be; "init" may not be. */
static void agent_fork(struct sched_state *s, int tid, bool context_switch)
{
	struct agent *a = MM_MALLOC(1, struct agent);
	assert(a && "failed to allocate new agent");
	
	a->tid = tid;
	a->action.handling_timer = false;
	/* XXX: may not be true in some kernels; also a huge hack */
	a->action.context_switch = context_switch;
	a->action.forking = false; /* XXX: may not be true in some kernels */
	a->action.sleeping = false;
	a->action.vanishing = false;
	a->action.readlining = false;
	a->action.schedule_target = false;
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

static void agent_wake(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid_or_null(&s->dq, tid);
	if (a) {
		Q_REMOVE(&s->dq, a, nobe);
	} else {
		a = agent_by_tid(&s->sq, tid);
		Q_REMOVE(&s->sq, a, nobe);
	}
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

static void agent_deschedule(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid(&s->rq, tid);
	Q_REMOVE(&s->rq, a, nobe);
	Q_INSERT_FRONT(&s->dq, a, nobe);
}

static void agent_sleep(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid(&s->rq, tid);
	Q_REMOVE(&s->rq, a, nobe);
	Q_INSERT_FRONT(&s->sq, a, nobe);
}

static void agent_vanish(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid(&s->rq, tid);
	Q_REMOVE(&s->rq, a, nobe);
	assert(a == s->cur_agent);
	/* It turns out kernels tend to have vanished threads continue to be the
	 * "current thread" after our trigger point. It's only safe to free them
	 * after somebody else gets scheduled. */
	if (s->last_vanished_agent) {
		assert(!s->last_vanished_agent->action.handling_timer);
		assert(s->last_vanished_agent->action.context_switch);
		MM_FREE(s->last_vanished_agent);
	}
	s->last_vanished_agent = a;
}

/******************************************************************************
 * Scheduler
 ******************************************************************************/

void sched_init(struct sched_state *s)
{
	Q_INIT_HEAD(&s->rq);
	Q_INIT_HEAD(&s->dq);
	kern_init_runqueue(s, agent_fork);
	s->cur_agent = agent_by_tid(&s->rq, kern_get_first_tid());
	s->context_switch_pending = false;
	s->context_switch_target = 0xdeadd00d; /* poison value */
	s->last_vanished_agent = NULL;
	s->guest_init_done = false;
	s->schedule_in_flight = NULL;
	s->entering_timer = false;
}

void print_agent(struct agent *a)
{
	printf("%d", a->tid);
	if (a->action.handling_timer)  printf("t");
	if (a->action.context_switch)  printf("c");
	if (a->action.forking)         printf("f");
	if (a->action.sleeping)        printf("s");
	if (a->action.vanishing)       printf("v");
	if (a->action.readlining)      printf("r");
	if (a->action.schedule_target) printf("*");
}

void print_q(const char *start, struct agent_q *q, const char *end)
{
	struct agent *a;
	bool first = true;

	printf("%s", start);
	Q_FOREACH(a, q, nobe) {
		if (first)
			first = false;
		else
			printf(", ");
		print_agent(a);
	}
	printf("%s", end);
}
void print_qs(struct sched_state *s)
{
	printf("current ");
	print_agent(s->cur_agent);
	printf(" ");
	print_q(" RQ [", &s->rq, "] ");
	print_q(" SQ {", &s->sq, "} ");
	print_q(" DQ (", &s->dq, ") ");
}

/* what is the current thread doing? */
#define ACTION(s, act) ((s)->cur_agent->action.act)
#define HANDLING_INTERRUPT(s) (ACTION(s, handling_timer)) /* FIXME: add kbd */
#define NO_ACTION(s) (!(ACTION(s, handling_timer) || ACTION(s, context_switch) \
			|| ACTION(s, forking) || ACTION(s, sleeping)           \
			|| ACTION(s, vanishing) || ACTION(s, readlining)))


void sched_update(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	int old_tid = s->cur_agent->tid;
	int new_tid = kern_get_current_tid(ls->cpu0);

	/* wait until the guest is ready */
	if (!s->guest_init_done) {
		if (kern_sched_init_done(ls->eip)) {
			s->guest_init_done = true;
			assert(old_tid == new_tid && "init tid mismatch");
		} else {
			return;
		}
	}

	/* The Importance of Being Assertive, A Trivial Style Guideline for
	 * Serious Programmers, by Ben Blum */
	if (s->entering_timer) {
		assert(ls->eip == kern_get_timer_wrap_begin() &&
		       "simics is a clown and tried to delay our interrupt :<");
		s->entering_timer = false;
	}

	/**********************************************************************
	 * Update scheduler state.
	 **********************************************************************/

	if (old_tid != new_tid && !s->context_switch_pending) {
		/* Careful! On some kernels, the trigger for a new agent forking
		 * (where it first gets added to the RQ) may happen AFTER its
		 * tcb is set to be the currently running thread. This would
		 * cause this case to be reached before agent_fork() is called,
		 * so agent_by_tid would fail. Instead, we have an option to
		 * find it later. (see the kern_thread_runnable case below.) */
		struct agent *next = agent_by_tid_or_null(&s->rq, new_tid);
		if (next) {
			lsprintf("switched threads %d -> %d\n", old_tid,
				 new_tid);
			s->cur_agent = next;
		} else {
			/* there is also a possibility of searching s->dq, to
			 * find the thread, but the kern_thread_runnable case
			 * below can handle it for us anyway with less code. */
			lsprintf("about to switch threads %d -> %d\n", old_tid,
				 new_tid);
			s->context_switch_pending = true;
			s->context_switch_target = new_tid;
		}
	}

	int target_tid;

	/* Timer interrupt handling. */
	if (kern_timer_entering(ls->eip)) {
		/* TODO: would it be right to assert !handling_timer? */
		ACTION(s, handling_timer) = true;
	} else if (kern_timer_exiting(ls->eip)) {
		assert(ACTION(s, handling_timer));
		ACTION(s, handling_timer) = false;
		/* If the schedule target was in a timer interrupt when we
		 * decided to schedule him, then now is when the operation
		 * finishes landing. (otherwise, see below) */
		if (ACTION(s, schedule_target)) {
			ACTION(s, schedule_target) = false;
		}
	/* Context switching. */
	} else if (kern_context_switch_entering(ls->eip)) {
		/* It -is- possible for a context switch to interrupt a
		 * context switch if a timer goes off before c-s disables
		 * interrupts. TODO: if we care, make this an int counter. */
		ACTION(s, context_switch) = true;
	} else if (kern_context_switch_exiting(ls->eip)) {
		assert(ACTION(s, context_switch));
		ACTION(s, context_switch) = false;
		/* For threads that context switched of their own accord. */
		if (ACTION(s, schedule_target) && !HANDLING_INTERRUPT(s)) {
			ACTION(s, schedule_target) = false;
		}
	/* Lifecycle. */
	} else if (kern_forking(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, forking) = true;
	} else if (kern_sleeping(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, sleeping) = true;
	} else if (kern_vanishing(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, vanishing) = true;
	} else if (kern_readline_enter(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, readlining) = true;
	} else if (kern_readline_exit(ls->eip)) {
		assert(ACTION(s, readlining));
		ACTION(s, readlining) = false;
	/* Runnable state change (incl. consequences of fork, vanish, sleep). */
	} else if (kern_thread_runnable(ls->cpu0, ls->eip, &target_tid)) {
		/* A thread is about to become runnable. Was it just spawned? */
		if (ACTION(s, forking) && !HANDLING_INTERRUPT(s)) {
			lsprintf("agent %d forked -- ", target_tid);
			agent_fork(s, target_tid, kern_fork_returns_to_cs());
			/* don't need this flag anymore; fork only forks once */
			ACTION(s, forking) = false;
		} else {
			lsprintf("agent %d wake -- ", target_tid);
			agent_wake(s, target_tid);
		}
		/* If this is happening from the context switcher, we also need
		 * to update the currently-running thread. */
		if (s->context_switch_pending) {
			assert(s->context_switch_target == target_tid);
			s->cur_agent = agent_by_tid(&s->rq, target_tid);
			s->context_switch_pending = false;
		}
		print_qs(s);
		printf("\n");
	} else if (kern_thread_descheduling(ls->cpu0, ls->eip, &target_tid)) {
		/* A thread is about to deschedule. Is it vanishing? */
		if (ACTION(s, vanishing) && !HANDLING_INTERRUPT(s)) {
			assert(s->cur_agent->tid == target_tid);
			agent_vanish(s, target_tid);
			lsprintf("agent %d vanished -- ", target_tid);
			/* Future actions by this thread, such as scheduling
			 * somebody else, shouldn't count as them vanishing too! */
			ACTION(s, vanishing) = false;
		} else if (ACTION(s, sleeping) && !HANDLING_INTERRUPT(s)) {
			assert(s->cur_agent->tid == target_tid);
			agent_sleep(s, target_tid);
			lsprintf("agent %d sleep -- ", target_tid);
			ACTION(s, sleeping) = false;
		} else {
			agent_deschedule(s, target_tid);
			lsprintf("agent %d desch -- ", target_tid);
		}
		print_qs(s);
		printf("\n");
	}

	/**********************************************************************
	 * Exercise our will upon the guest kernel
	 **********************************************************************/

	/* Some checks before invoking the arbiter. First see if an operation of
	 * ours is already in-flight. */
	if (s->schedule_in_flight) {
		if (s->schedule_in_flight == s->cur_agent) {
			/* the in-flight schedule operation is cleared for
			 * landing. note that this may cause another one to
			 * be triggered again as soon as the context switcher
			 * and/or the timer handler finishes; it is up to the
			 * arbiter to decide this. */
			assert(ACTION(s, schedule_target));
			/* this condition should trigger in the middle of the
			 * switch, rather than after it finishes. (which is also
			 * why we leave the schedule_target flag turned on. */
			assert(ACTION(s, context_switch) ||
			       HANDLING_INTERRUPT(s));
			s->schedule_in_flight = NULL;
		} else {
			/* An undesirable thread has been context-switched away
			 * from either from an interrupt handler (timer/kbd) or
			 * of its own accord. We need to wait for it to get back
			 * to its own execution before triggering an interrupt
			 * on it; in the former case, this will be just after it
			 * irets; in the latter, just after the c-s returns. */
			if (kern_timer_exiting(ls->eip) ||
			    (!HANDLING_INTERRUPT(s) &&
			     kern_context_switch_exiting(ls->eip))) {
				/* an undesirable agent just got switched to;
				 * keep the pending schedule in the air. */
				lsprintf("keeping schedule in-flight at 0x%x\n",
					 ls->eip);
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
			} else {
				/* they'd better not have "escaped" */
				assert(ACTION(s, context_switch) ||
				       HANDLING_INTERRUPT(s));
			}
			/* in any case we have no more decisions to make here */
			return;
		}
	}
	assert(!s->schedule_in_flight);

	/* XXX TODO: This will "leak" an undesirable thread to execute an
	 * instruction if the timer/kbd handler is an interrupt gate, so check
	 * also if we're about to iret and then examine the eflags on the
	 * stack. Also, "sti" and "popf" are interesting, so check for those.
	 * Also, do trap gates enable interrupts if they were off? o_O */
	if (!interrupts_enabled(ls->cpu0)) {
		return;
	}

	/* If a schedule operation is just finishing, we should allow the thread
	 * to get back to its own execution before making another choice. Note 
	 * that when we previously decided to interrupt the thread, it will have
	 * executed the single instruction we made the choice at then taken the
	 * interrupt, so we return to the next instruction, not the same one. */
	if (ACTION(s, schedule_target)) {
		return;
	}

	/* TODO: have an extra mode which will allow us to preempt the timer
	 * handler. */
	if (HANDLING_INTERRUPT(s) || kern_scheduler_locked(ls->cpu0)) {
		return;
	}

	/* Okay, are we at a choice point? */
	/* TODO: arbiter may also want to see the trace_entry_t */
	if (arbiter_interested(ls)) {
		struct agent *a;
		bool our_choice;
		/* TODO: as an optimisation (in serialisation state / etc), the
		 * arbiter may return NULL if there was only one possible
		 * choice. */
		if (arbiter_choose(ls, &a, &our_choice)) {
			/* Effect the choice that was made... */
			if (a != s->cur_agent) {
				lsprintf("from agent %d, arbiter chose %d at "
					 "0x%x (called at 0x%x)\n",
					 s->cur_agent->tid, a->tid, ls->eip,
					 (unsigned int)READ_STACK(ls->cpu0, 0));
				s->schedule_in_flight = a;
				a->action.schedule_target = true;
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
			}
			/* Record the choice that was just made. */
			save_setjmp(&ls->save, ls, a->tid, our_choice);
		} else {
			lsprintf("no agent was chosen at eip 0x%x\n", ls->eip);
		}
	}
	/* XXX TODO: it may be that not every timer interrupt triggers a context
	 * switch, so we should watch out if a handler doesn't enter the c-s. */
}