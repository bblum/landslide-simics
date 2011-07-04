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

/******************************************************************************
 * Agence
 ******************************************************************************/

static void new_agent(struct sched_state *s, int tid)
{
	struct agent *a = MM_MALLOC(1, struct agent);
	assert(a && "failed to allocate new agent");
	
	a->tid = tid;
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

static void del_agent(struct sched_state *s, int tid)
{
	struct agent *a;
	Q_SEARCH(a, &s->rq, nobe, a->tid == tid);
	assert(a && "attempt to remove tid not in rq");
	Q_REMOVE(&s->rq, a, nobe);
}

/******************************************************************************
 * Scheduler
 ******************************************************************************/

void sched_init(struct sched_state *s)
{
	Q_INIT_HEAD(&s->rq);
	s->current_thread = kern_get_init_thread();
	kern_init_runqueue(s, new_agent);
	s->schedule_in_progress = false;
}

static void print_rq(struct sched_state *s)
{
	struct agent *a;
	bool first = true;

	printf("[");
	Q_FOREACH(a, &s->rq, nobe) {
		if (first)
			first = false;
		else
			printf(", ");
		printf("%d", a->tid);
	}
	printf("]");
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
		int tid = kern_thread_appearing(ls);
		new_agent(&ls->sched, tid);
		printf("new agent %d at eip 0x%x -- ", tid, ls->eip);
		print_rq(&ls->sched);
		printf("\n");
	} else if (kern_thread_is_disappearing(ls)) {
		int tid = kern_thread_disappearing(ls);
		del_agent(&ls->sched, tid);
		printf("agent %d gone at eip 0x%x -- ", tid, ls->eip);
		print_rq(&ls->sched);
		printf("\n");
	}
}
