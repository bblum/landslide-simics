/**
 * @file arbiter.c
 * @author Ben Blum
 * @brief decision-making routines for landslide
 */

#include <stdio.h>
#include <stdlib.h>

#define MODULE_NAME "ARBITER"
#define MODULE_COLOUR COLOUR_YELLOW

#include "common.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "schedule.h"
#include "x86.h"

void arbiter_init(struct arbiter_state *r)
{
	Q_INIT_HEAD(&r->choices);
}

// FIXME: do these need to be threadsafe?
void arbiter_append_choice(struct arbiter_state *r, int tid)
{
	struct choice *c = MM_XMALLOC(1, struct choice);
	c->tid = tid;
	Q_INSERT_FRONT(&r->choices, c, nobe);
}

bool arbiter_pop_choice(struct arbiter_state *r, int *tid)
{
	struct choice *c = Q_GET_TAIL(&r->choices);
	if (c) {
		lsprintf("using requested tid %d\n", c->tid);
		Q_REMOVE(&r->choices, c, nobe);
		*tid = c->tid;
		MM_FREE(c);
		return true;
	} else {
		return false;
	}
}

bool arbiter_interested(struct ls_state *ls)
{
	// TODO: more interesting choice points

	/* Attempt to see if a "voluntary" reschedule is just ending - did the
	 * last thread context switch not because of a timer? */
	if (ls->sched.last_agent != NULL &&
	    !ls->sched.last_agent->action.handling_timer) {
		/* And the current thread is just resuming execution? Either
		 * exiting the timer handler, */
		if ((kern_timer_exiting(ls->eip) &&
		     // XXX: see kern_timer_exiting case in schedule.c for why.
		     !kern_timer_exiting(READ_STACK(ls->cpu0, 0))) ||
		    /* ...or never was in timer and exiting context switch. */
		    (!ls->sched.cur_agent->action.handling_timer &&
		     kern_context_switch_exiting(ls->eip)) ||
		    /* ...or this. */
		    kern_fork_return_spot(ls->eip)) {
			lsprintf("a voluntary reschedule: ");
			print_agent(ls->sched.last_agent);
			printf(" to ");
			print_agent(ls->sched.cur_agent);
			printf("\n");
			assert((ls->save.next_tid == -1 ||
			        ls->save.next_tid == ls->sched.cur_agent->tid ||
			        ls->save.next_tid == ls->sched.last_agent->tid)
			       && "Two threads in one transition?");
			return true;
		}
	}

	if (ls->eip == GUEST_MUTEX_LOCK &&
	    !kern_mutex_ignore(READ_STACK(ls->cpu0,
	                       GUEST_MUTEX_LOCK_MUTEX_ARGNUM)) &&
	    within_function(ls->cpu0, ls->eip,
	                    GUEST_VANISH, GUEST_VANISH_END)) {
		assert((ls->save.next_tid == -1 ||
		       ls->save.next_tid == ls->sched.cur_agent->tid) &&
		       "Two threads in one transition?");
		return true;
	} else {
		return false;
	}
}

/* Returns true if a thread was chosen. If true, sets 'target' (to either the
 * current thread or any other thread), and sets 'our_choice' to false if
 * somebody else already made this choice for us, true otherwise. */
bool arbiter_choose(struct ls_state *ls, struct agent **target,
		    bool *our_choice)
{
	struct agent *a;
	int count = 0;

	/* We shouldn't be asked to choose if somebody else already did. */
	assert(Q_GET_SIZE(&ls->arbiter.choices) == 0);

	/* Count the number of available threads. */
	Q_FOREACH(a, &ls->sched.rq, nobe) {
		if (!BLOCKED(a))
			count++;
	}
	Q_FOREACH(a, &ls->sched.sq, nobe) {
		if (!BLOCKED(a))
			count++;
	}

	count = 1; // Comment this out to enable "hard mode"
	int i = 0;
	Q_SEARCH(a, &ls->sched.rq, nobe, !BLOCKED(a) && ++i == count);
	if (a == NULL)
		Q_SEARCH(a, &ls->sched.sq, nobe, !BLOCKED(a) && ++i == count);
	if (a != NULL) {
		lsprintf("Figured I'd look at TID %d next.\n", a->tid);
		*target = a;
		*our_choice = true;
		return true;
	} else {
		return false;
	}
}
