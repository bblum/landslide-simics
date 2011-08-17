/**
 * @file arbiter.c
 * @author Ben Blum
 * @brief decision-making routines for landslide
 */

#include <stdio.h>
#include <stdlib.h>

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
	struct choice *c = MM_MALLOC(1, struct choice);
	assert(c && "failed to allocate arbiter choice");
	c->tid = tid;
	Q_INSERT_FRONT(&r->choices, c, nobe);
}

static bool arbiter_pop_choice(struct arbiter_state *r, int *tid)
{
	struct choice *c = Q_GET_TAIL(&r->choices);
	if (c) {
		Q_REMOVE(&r->choices, c, nobe);
		*tid = c->tid;
		MM_FREE(c);
		return true;
	} else {
		return false;
	}
}


// TODO move this elsewhere
// #define GUEST_MUTEX_LOCK 0x001068a0
#define GUEST_MUTEX_LOCK 0x106930
#define GUEST_VANISH 0x104223
#define GUEST_VANISH_END 0x104593

bool arbiter_interested(struct ls_state *ls)
{
	// TODO: more interesting choice points
	int called_from = READ_STACK(ls->cpu0, 0);
	return ls->eip == GUEST_MUTEX_LOCK
	    && called_from >= GUEST_VANISH
	    && called_from <= GUEST_VANISH_END;
}

#define BUF_SIZE 512

/* Returns true if a thread was chosen. If true, sets 'target' (to either the
 * current thread or any other thread), and sets 'our_choice' to false if
 * somebody else already made this choice for us, true otherwise. */
bool arbiter_choose(struct ls_state *ls, struct agent **target,
		    bool *our_choice)
{
	struct arbiter_state *r = &ls->arbiter;
	struct agent *a;
	int tid;

	/* Has the user specified something for us to do? */
	if (arbiter_pop_choice(r, &tid)) {
		printf("[ARBITER] looking for requested agent %d\n", tid);
		a = agent_by_tid_or_null(&ls->sched.rq, tid);
		if (!a) {
			a = agent_by_tid_or_null(&ls->sched.sq, tid);
		}
		if (a) {
			*target = a;
			*our_choice = false;
			return true;
		} else {
			printf("[ARBITER] failed to choose agent %d\n", tid);
			return false;
		}
	/* automatically choose a thread */
	} else {
		/* TODO: sleep queue */
		int size = Q_GET_SIZE(&ls->sched.rq);
		int i = 0;

		Q_SEARCH(a, &ls->sched.rq, nobe, ++i == size);
		if (a) {
			*target = a;
			*our_choice = true;
			return true;
		} else {
			return false;
		}
	}
}
