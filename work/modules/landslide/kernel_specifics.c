/**
 * @file kernel_specifics.c
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#include "landslide.h"

/* Assumptions we need to make about the kernel */

#define WORD_SIZE 4 /* TODO move to x86-up.h */

#define GUEST_POBBLES
#ifdef GUEST_POBBLES

/* Where to find the currently running TCB pointer */
#define GUEST_CURRENT_TCB 0x00152420
#define GUEST_TCB_TID_OFFSET 0
#define TID_FROM_TCB(ls, tcb) \
	SIM_read_phys_memory(ls->cpu0, tcb + GUEST_TCB_TID_OFFSET, WORD_SIZE)

/* Where is it that agents appear and disappear? */
#define GUEST_Q_ADD                0x00105613
#define GUEST_Q_ADD_TCB_ARGNUM     2 /* second argument; rq should be first */
#define GUEST_Q_REMOVE             0x001056d5
#define GUEST_Q_REMOVE_TCB_ARGNUM  2 /* same as above */
#define GUEST_Q_POP_RETURN         0x1056d4 /* thread in eax; discus */
/* XXX: this is discus; for better interface maybe make an enum type for these
 * functions wherein one takes the thread as an arg and others return it...
 * a challenge: use symtab to extract either the start or the end of a fn */


// TODO: elsif ...
#endif

/* Returns the tcb/tid of the currently scheduled thread. */
int get_current_tcb(ls_state_t *ls)
{
	return SIM_read_phys_memory(ls->cpu0, GUEST_CURRENT_TCB, WORD_SIZE);
}

int get_current_tid(ls_state_t *ls)
{
	return TID_FROM_TCB(ls, get_current_tcb(ls));
}

/* How to tell if a new agent is appearing or disappearing. */
bool agent_is_appearing(ls_state_t *ls)
{
	return GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_ADD;
}

int agent_appearing(ls_state_t *ls)
{
	assert(agent_is_appearing(ls));
	/* 0(%esp) points to the return address; get the arg above it */
	int arg_loc = GET_CPU_ATTR(ls->cpu0, esp) +
		(GUEST_Q_ADD_TCB_ARGNUM * WORD_SIZE);
	return TID_FROM_TCB(ls, SIM_read_phys_memory(ls->cpu0, arg_loc,
						     WORD_SIZE));
}

bool agent_is_disappearing(ls_state_t *ls)
{
	return GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_REMOVE
	    || GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_POP_RETURN;
}

int agent_disappearing(ls_state_t *ls)
{
	assert(agent_is_disappearing(ls));
	
	int tcb;

	if (GET_CPU_ATTR(ls->cpu0, eip) == GUEST_Q_REMOVE) {
		/* at beginning of sch_queue_remove */
		int arg_loc = GET_CPU_ATTR(ls->cpu0, esp) +
			(GUEST_Q_REMOVE_TCB_ARGNUM * WORD_SIZE);
		tcb = SIM_read_phys_memory(ls->cpu0, arg_loc, WORD_SIZE);
	} else {
		/* at end of sch_queue_pop; see prior assert */
		tcb = GET_CPU_ATTR(ls->cpu0, eax);
	}
	return TID_FROM_TCB(ls, tcb);
}
