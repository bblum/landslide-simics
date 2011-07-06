/**
 * @file kernel_specifics.c
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#include "landslide.h"
#include "schedule.h" /* TODO: separate the struct part into schedule_type.h */

/* Assumptions we need to make about the kernel */

#define GUEST_POBBLES
#ifdef GUEST_POBBLES

/* Where to find the currently running TCB pointer */
#define GUEST_CURRENT_TCB 0x00152420
#define GUEST_TCB_TID_OFFSET 0
#define TID_FROM_TCB(ls, tcb) \
	SIM_read_phys_memory(ls->cpu0, tcb + GUEST_TCB_TID_OFFSET, WORD_SIZE)

#define GUEST_RQ_ADDR 0x00152424

/* Where is it that runnable threads appear and disappear? */
#define GUEST_Q_ADD                0x00105613
#define GUEST_Q_ADD_Q_ARGNUM       1 /* rq to modify is first argument */
#define GUEST_Q_ADD_TCB_ARGNUM     2 /* target tcb is second argument */
#define GUEST_Q_REMOVE             0x001056d5
#define GUEST_Q_REMOVE_Q_ARGNUM    1 /* same as above */
#define GUEST_Q_REMOVE_TCB_ARGNUM  2 /* same as above */
#define GUEST_Q_POP_RETURN         0x001056d4 /* thread in eax; discus */
#define GUEST_Q_POP_Q_ARGNUM       1
/* XXX: this is discus; for better interface maybe make an enum type for these
 * functions wherein one takes the thread as an arg and others return it...
 * a challenge: use symtab to extract either the start or the end of a fn */

/* Interrupt handler information */
#define GUEST_TIMER_WRAP_ENTER     0x001035bc
#define GUEST_TIMER_WRAP_EXIT      0x001035c3 /* should always be "iret" */
/* When is it safe to assume init/idle are initialised? */
#define GUEST_SCHED_INIT_EXIT      0x001053ed

/* Windows around the lifecycle-changing parts of fork and vanish, for
 * determining when a thread's life begins and ends. The fork window should
 * include only the instructions which cause the child thread to be added to
 * the runqueue, and the vanish window should include only the instructions
 * which cause the final removal of the vanishing thread from the runqueue.
 * NOTE: A kernel may have more points than these; this is not generalised. */
#define GUEST_FORK_WINDOW_ENTER    0x00103ec0
#define GUEST_THRFORK_WINDOW_ENTER 0x001041bb
#define GUEST_SLEEP_WINDOW_ENTER   0x00103fb2
#define GUEST_VANISH_WINDOW_ENTER  0x0010450d


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

bool kern_sched_init_done(struct ls_state *ls)
{
	return ls->eip == GUEST_SCHED_INIT_EXIT;
}

/* TODO: kern_scheduler_is_locked() and how does it interact with sleepers? */

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
			void (*add_thread)(struct sched_state *, int))
{
	/* Only init runs first in POBBLES, but other kernels may have idle. */
	add_thread(s, 1);
}
