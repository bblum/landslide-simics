/**
 * @file kernel_specifics.c
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

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
int kern_get_current_tcb(conf_object_t *cpu)
{
	return SIM_read_phys_memory(cpu, GUEST_CURRENT_TCB, WORD_SIZE);
}
int kern_get_current_tid(conf_object_t *cpu)
{
	return TID_FROM_TCB(cpu, kern_get_current_tcb(cpu));
}

/* The boundaries of the timer handler wrapper. */
bool kern_timer_entering(int eip)
{
	return eip == GUEST_TIMER_WRAP_ENTER;
}
bool kern_timer_exiting(int eip)
{
	return eip == GUEST_TIMER_WRAP_EXIT;
}
int kern_get_timer_wrap_begin()
{
	return GUEST_TIMER_WRAP_ENTER;
}

/* the boundaries of the context switcher */
bool kern_context_switch_entering(int eip)
{
	return eip == GUEST_CONTEXT_SWITCH_ENTER;
}
bool kern_context_switch_exiting(int eip)
{
	return eip == GUEST_CONTEXT_SWITCH_EXIT;
}

bool kern_sched_init_done(int eip)
{
	return eip == GUEST_SCHED_INIT_EXIT;
}

/* Anything that would prevent timer interrupts from triggering context
 * switches */
bool kern_scheduler_locked(conf_object_t *cpu)
{
	int x = SIM_read_phys_memory(cpu, GUEST_SCHEDULER_LOCK, WORD_SIZE);
	return GUEST_SCHEDULER_LOCKED(x);
}

/******************************************************************************
 * Lifecycle
 ******************************************************************************/

/* How to tell if a thread's life is beginning or ending */
bool kern_forking(int eip)
{
	return (eip == GUEST_FORK_WINDOW_ENTER)
	    || (eip == GUEST_THRFORK_WINDOW_ENTER);
}
bool kern_sleeping(int eip)
{
	return eip == GUEST_SLEEP_WINDOW_ENTER;
}
bool kern_vanishing(int eip)
{
	return eip == GUEST_VANISH_WINDOW_ENTER;
}
bool kern_readline_enter(int eip)
{
	return eip == GUEST_READLINE_WINDOW_ENTER;
}
bool kern_readline_exit(int eip)
{
	return eip == GUEST_READLINE_WINDOW_EXIT;
}

/* How to tell if a new thread is appearing or disappearing on the runqueue. */
static bool thread_becoming_runnable(conf_object_t *cpu, int eip)
{
	return (eip == GUEST_Q_ADD)
	    && (READ_STACK(cpu, GUEST_Q_ADD_Q_ARGNUM) == GUEST_RQ_ADDR);
}
bool kern_thread_runnable(conf_object_t *cpu, int eip, int *tid)
{
	if (thread_becoming_runnable(cpu, eip)) {
		/* 0(%esp) points to the return address; get the arg above it */
		*tid = TID_FROM_TCB(cpu, READ_STACK(cpu,
						    GUEST_Q_ADD_TCB_ARGNUM));
		return true;
	} else {
		return false;
	}
}

static bool thread_is_descheduling(conf_object_t *cpu, int eip)
{
	return ((eip == GUEST_Q_REMOVE)
	     && (READ_STACK(cpu, GUEST_Q_REMOVE_Q_ARGNUM) == GUEST_RQ_ADDR))
	    || ((eip == GUEST_Q_POP_RETURN)
	     && (READ_STACK(cpu, GUEST_Q_POP_Q_ARGNUM) == GUEST_RQ_ADDR));
}
bool kern_thread_descheduling(conf_object_t *cpu, int eip, int *tid)
{
	if (thread_is_descheduling(cpu, eip)) {
		int tcb;
		if (eip == GUEST_Q_REMOVE) {
			/* at beginning of sch_queue_remove */
			tcb = READ_STACK(cpu, GUEST_Q_REMOVE_TCB_ARGNUM);
		} else {
			/* at end of sch_queue_pop; see prior assert */
			tcb = GET_CPU_ATTR(cpu, eax);
		}
		*tid = TID_FROM_TCB(cpu, tcb);
		return true;
	} else {
		return false;
	}
}

/******************************************************************************
 * Other / Init
 ******************************************************************************/

int kern_get_init_tid()
{
	return 1;
}

int kern_get_idle_tid()
{
	assert(false && "POBBLES does not have an idle tid!");
}

/* the tid of the shell (OK to assume the first shell never exits). */
int kern_get_shell_tid()
{
	return 2;
}

/* Which thread runs first on kernel init? */
int kern_get_first_tid()
{
	return 1;
}

void kern_init_runqueue(struct sched_state *s,
			void (*add_thread)(struct sched_state *, int, bool))
{
	/* Only init runs first in POBBLES, but other kernels may have idle. In
	 * POBBLES, init is not context-switched to to begin with. */
	add_thread(s, kern_get_init_tid(), false);
}

/* Do newly forked children exit to userspace through the end of the
 * context-switcher? (POBBLES does not; it bypasses the end to return_zero.) */
bool kern_fork_returns_to_cs()
{
	return false;
}