/**
 * @file kernel_specifics.c
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#include "landslide.h"
#include "schedule.h" /* TODO: separate the struct part into schedule_type.h */
#include "x86.h"

/* Assumptions we need to make about the kernel */

#define GUEST_POBBLES_RACE

#if defined(GUEST_POBBLES)
#include "kernel_specifics_pobbles.h"
#elif defined(GUEST_POBBLES_RACE)
#include "kernel_specifics_pobbles_race.h"

// TODO: elsif ...
#endif

/******************************************************************************
 * Miscellaneous information
 ******************************************************************************/

/* Returns the tcb/tid of the currently scheduled thread. */
int kern_get_current_tcb(struct ls_state *ls)
{
	return SIM_read_phys_memory(ls->cpu0, GUEST_CURRENT_TCB, WORD_SIZE);
}
int kern_get_current_tid(struct ls_state *ls)
{
	return TID_FROM_TCB(ls, kern_get_current_tcb(ls));
}

/* The boundaries of the timer handler wrapper. */
bool kern_timer_entering(struct ls_state *ls)
{
	return ls->eip == GUEST_TIMER_WRAP_ENTER;
}
bool kern_timer_exiting(struct ls_state *ls)
{
	return ls->eip == GUEST_TIMER_WRAP_EXIT;
}
int kern_get_timer_wrap_begin()
{
	return GUEST_TIMER_WRAP_ENTER;
}

/* the boundaries of the context switcher */
bool kern_context_switch_entering(struct ls_state *ls)
{
	return ls->eip == GUEST_CONTEXT_SWITCH_ENTER;
}
bool kern_context_switch_exiting(struct ls_state *ls)
{
	return ls->eip == GUEST_CONTEXT_SWITCH_EXIT;
}

bool kern_sched_init_done(struct ls_state *ls)
{
	return ls->eip == GUEST_SCHED_INIT_EXIT;
}

/* Anything that would prevent timer interrupts from triggering context
 * switches */
bool kern_scheduler_locked(struct ls_state *ls)
{
	int x = SIM_read_phys_memory(ls->cpu0, GUEST_SCHEDULER_LOCK, WORD_SIZE);
	return GUEST_SCHEDULER_LOCKED(x);
}

/******************************************************************************
 * Lifecycle
 ******************************************************************************/

/* How to tell if a thread's life is beginning or ending */
bool kern_forking(struct ls_state *ls)
{
	return (ls->eip == GUEST_FORK_WINDOW_ENTER)
	    || (ls->eip == GUEST_THRFORK_WINDOW_ENTER);
}
bool kern_sleeping(struct ls_state *ls)
{
	return ls->eip == GUEST_SLEEP_WINDOW_ENTER;
}
bool kern_vanishing(struct ls_state *ls)
{
	return ls->eip == GUEST_VANISH_WINDOW_ENTER;
}

/* How to tell if a new thread is appearing or disappearing on the runqueue. */
static bool thread_becoming_runnable(struct ls_state *ls)
{
	return (ls->eip == GUEST_Q_ADD)
	    && (READ_STACK(ls->cpu0, GUEST_Q_ADD_Q_ARGNUM) == GUEST_RQ_ADDR);
}
bool kern_thread_runnable(struct ls_state *ls, int *tid)
{
	if (thread_becoming_runnable(ls)) {
		/* 0(%esp) points to the return address; get the arg above it */
		*tid = TID_FROM_TCB(ls, READ_STACK(ls->cpu0,
						   GUEST_Q_ADD_TCB_ARGNUM));
		return true;
	} else {
		return false;
	}
}

static bool thread_is_descheduling(struct ls_state *ls)
{
	return ((ls->eip == GUEST_Q_REMOVE)
	     && (READ_STACK(ls->cpu0, GUEST_Q_REMOVE_Q_ARGNUM) == GUEST_RQ_ADDR))
	    || ((ls->eip == GUEST_Q_POP_RETURN)
	     && (READ_STACK(ls->cpu0, GUEST_Q_POP_Q_ARGNUM) == GUEST_RQ_ADDR));
}
bool kern_thread_descheduling(struct ls_state *ls, int *tid)
{
	if (thread_is_descheduling(ls)) {
		int tcb;
		if (ls->eip == GUEST_Q_REMOVE) {
			/* at beginning of sch_queue_remove */
			tcb = READ_STACK(ls->cpu0, GUEST_Q_REMOVE_TCB_ARGNUM);
		} else {
			/* at end of sch_queue_pop; see prior assert */
			tcb = GET_CPU_ATTR(ls->cpu0, eax);
		}
		*tid = TID_FROM_TCB(ls, tcb);
		return true;
	} else {
		return false;
	}
}

/******************************************************************************
 * Other / Init
 ******************************************************************************/

/* Which thread runs first on kernel init? */
int kern_get_init_tid()
{
	return 1;
}

void kern_init_runqueue(struct sched_state *s,
			void (*add_thread)(struct sched_state *, int, bool))
{
	/* Only init runs first in POBBLES, but other kernels may have idle. In
	 * POBBLES, init is not context-switched to to begin with. */
	add_thread(s, 1, false);
}

/* Do newly forked children exit to userspace through the end of the
 * context-switcher? (POBBLES does not; it bypasses the end to return_zero.) */
bool kern_fork_returns_to_cs()
{
	return false;
}
