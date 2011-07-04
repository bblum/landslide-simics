/**
 * @file kernel_specifics.c
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#include "landslide.h"

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
#define GUEST_Q_POP_RETURN         0x1056d4 /* thread in eax; discus */
#define GUEST_Q_POP_Q_ARGNUM       1
/* XXX: this is discus; for better interface maybe make an enum type for these
 * functions wherein one takes the thread as an arg and others return it...
 * a challenge: use symtab to extract either the start or the end of a fn */


// TODO: elsif ...
#endif

/* Returns the tcb/tid of the currently scheduled thread. */
int kern_get_current_tcb(struct ls_state *ls)
{
	return SIM_read_phys_memory(ls->cpu0, GUEST_CURRENT_TCB, WORD_SIZE);
}

int kern_get_current_tid(struct ls_state *ls)
{
	return TID_FROM_TCB(ls, kern_get_current_tcb(ls));
}

/******************************************************************************
 * Lifecycle
 ******************************************************************************/

/* How to tell if a new thread is appearing or disappearing. */
bool kern_thread_is_appearing(struct ls_state *ls)
{
	return (GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_ADD)
	    && (READ_STACK(ls->cpu0, GUEST_Q_ADD_Q_ARGNUM) == GUEST_RQ_ADDR);
}

int kern_thread_appearing(struct ls_state *ls)
{
	assert(kern_thread_is_appearing(ls));
	/* 0(%esp) points to the return address; get the arg above it */
	return TID_FROM_TCB(ls, READ_STACK(ls->cpu0, GUEST_Q_ADD_TCB_ARGNUM));
}

bool kern_thread_is_disappearing(struct ls_state *ls)
{
	return ((GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_REMOVE)
	     && (READ_STACK(ls->cpu0, GUEST_Q_REMOVE_Q_ARGNUM) == GUEST_RQ_ADDR))
	    || ((GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_POP_RETURN)
	     && (READ_STACK(ls->cpu0, GUEST_Q_POP_Q_ARGNUM) == GUEST_RQ_ADDR));
}

int kern_thread_disappearing(struct ls_state *ls)
{
	assert(kern_thread_is_disappearing(ls));
	
	int tcb;

	if (GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_REMOVE) {
		/* at beginning of sch_queue_remove */
		tcb = READ_STACK(ls->cpu0, GUEST_Q_REMOVE_TCB_ARGNUM);
	} else {
		/* at end of sch_queue_pop; see prior assert */
		tcb = GET_CPU_ATTR(ls->cpu0, eax);
	}
	return TID_FROM_TCB(ls, tcb);
}

/******************************************************************************
 * Other / Init
 ******************************************************************************/

/* Which thread runs first on kernel init? */
int kern_get_init_thread()
{
	return 1;
}

void kern_init_runqueue(struct sched_state *s,
			void (*add_thread)(struct sched_state *, int))
{
	/* Only init runs first in POBBLES, but other kernels may have idle. */
	add_thread(s, 1);
}
