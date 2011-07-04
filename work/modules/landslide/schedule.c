/**
 * @file schedule.c
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#include <simics/api.h>
#include <simics/alloc.h>

#include "landslide.h"
#include "schedule.h"
#include "kernel_specifics.h"

static void new_agent(struct sched_state *s, int tid)
{
	struct agent *a = MM_MALLOC(1, struct agent);
	assert(a && "failed to allocate new agent");
	
	a->tid = tid;
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

void sched_init(struct sched_state *s)
{
	Q_INIT_HEAD(&s->rq);
	s->current_thread = kern_get_init_thread();
	kern_init_runqueue(s, new_agent);
	s->schedule_in_progress = false;
}

void sched_update(struct ls_state *ls)
{
	// TODO
	int old_thread = ls->sched.current_thread;
	ls->sched.current_thread = kern_get_current_tid(ls);
	if (old_thread != ls->sched.current_thread) {
		printf("switched threads %d -> %d at 0x%x\n", old_thread,
		       ls->sched.current_thread, ls->eip);
	}

	if (kern_thread_is_appearing(ls)) {
		printf("new agent %d at eip 0x%x\n", kern_thread_appearing(ls), ls->eip);
	} else if (kern_thread_is_disappearing(ls)) {
		printf("agent %d gone at eip 0x%x\n", kern_thread_disappearing(ls), ls->eip);
	}
}
