/**
 * @file arbiter.c
 * @author Ben Blum
 * @brief decision-making routines for landslide
 */

#include "landslide.h"
#include "schedule.h"

// TODO move this elsewhere
#define GUEST_MUTEX_LOCK 0x001068a0

bool arbiter_interested(struct ls_state *ls)
{
	return ls->eip == GUEST_MUTEX_LOCK;
}

/* May return either null, the current thread, or any other thread. */
struct agent *arbiter_choose(struct sched_state *s)
{
	/* TODO: sleep queue */
	int size = Q_GET_SIZE(&s->rq);
	int i = 0;
	struct agent *a;
	
	Q_SEARCH(a, &s->rq, nobe, ++i == size);
	return a;
}
