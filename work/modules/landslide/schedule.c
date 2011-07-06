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
#include "variable_queue.h"

/******************************************************************************
 * Agence
 ******************************************************************************/

static struct agent *agent_by_tid_or_null(struct agent_q *q, int tid)
{
	struct agent *a;
	Q_SEARCH(a, q, nobe, a->tid == tid);
	return a;
}

static struct agent *agent_by_tid(struct agent_q *q, int tid)
{
	struct agent *a = agent_by_tid_or_null(q, tid);
	assert(a && "attempt to look up tid not in specified queue");
	return a;
}

static void agent_fork(struct sched_state *s, int tid)
{
	struct agent *a = MM_MALLOC(1, struct agent);
	assert(a && "failed to allocate new agent");
	
	a->tid = tid;
	a->action.handling_timer = false;
	a->action.forking = false; /* XXX: may not be true in some kernels */
	a->action.sleeping = false;
	a->action.vanishing = false;
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

static void agent_wake(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid_or_null(&s->dq, tid);
	if (a) {
		Q_REMOVE(&s->dq, a, nobe);
	} else {
		a = agent_by_tid(&s->sq, tid);
		Q_REMOVE(&s->sq, a, nobe);
	}
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

static void agent_deschedule(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid(&s->rq, tid);
	Q_REMOVE(&s->rq, a, nobe);
	Q_INSERT_FRONT(&s->dq, a, nobe);
}

static void agent_sleep(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid(&s->rq, tid);
	Q_REMOVE(&s->rq, a, nobe);
	Q_INSERT_FRONT(&s->sq, a, nobe);
}

static void agent_vanish(struct sched_state *s, int tid)
{
	struct agent *a = agent_by_tid(&s->rq, tid);
	Q_REMOVE(&s->rq, a, nobe);
	/* It turns out kernels tend to have vanished threads continue to be the
	 * "current thread" after our trigger point. It's only safe to free them
	 * after somebody else gets scheduled. */
	if (s->last_vanished_agent)
		MM_FREE(s->last_vanished_agent);
	s->last_vanished_agent = a;
}

/******************************************************************************
 * Scheduler
 ******************************************************************************/

void sched_init(struct sched_state *s)
{
	Q_INIT_HEAD(&s->rq);
	Q_INIT_HEAD(&s->dq);
	kern_init_runqueue(s, agent_fork);
	s->cur_agent = agent_by_tid(&s->rq, kern_get_init_tid());
	s->context_switch_pending = false;
	s->context_switch_target = 0xdeadd00d; /* poison value */
	s->last_vanished_agent = NULL;
	s->guest_init_done = false;
	s->schedule_in_progress = false;
}

static void print_q(const char *start, struct agent_q *q, const char *end)
{
	struct agent *a;
	bool first = true;

	printf("%s", start);
	Q_FOREACH(a, q, nobe) {
		if (first)
			first = false;
		else
			printf(", ");
		printf("%d", a->tid);
		if (a->action.handling_timer)
			printf("t");
	}
	printf("%s", end);
}
static void print_qs(struct sched_state *s)
{
	print_q(" RQ [", &s->rq, "] ");
	print_q(" SQ {", &s->sq, "} ");
	print_q(" DQ (", &s->dq, ") ");
}

/* what is the current thread doing? */
#define ACTION(s, act) ((s)->cur_agent->action.act)
/* FIXME: add one for the keyboard */
#define HANDLING_INTERRUPT(s) (ACTION(s, handling_timer))

void sched_update(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	int old_tid = s->cur_agent->tid;
	int new_tid = kern_get_current_tid(ls);

	/* wait until the guest is ready */
	if (!s->guest_init_done) {
		if (kern_sched_init_done(ls)) {
			s->guest_init_done = true;
			assert(old_tid == new_tid);
		} else {
			return;
		}
	}

	if (old_tid != new_tid && !s->context_switch_pending) {
		/* Careful! On some kernels, the trigger for a new agent forking
		 * (where it first gets added to the RQ) may happen AFTER its
		 * tcb is set to be the currently running thread. This would
		 * cause this case to be reached before agent_fork() is called,
		 * so agent_by_tid would fail. Instead, we have an option to
		 * find it later. (see the kern_thread_runnable case below.) */
		struct agent *next = agent_by_tid_or_null(&s->rq, new_tid);
		if (next) {
			printf("switched threads %d -> %d\n", old_tid, new_tid);
			s->cur_agent = next;
		} else {
			/* there is also a possibility of searching s->dq, to
			 * find the thread, but the kern_thread_runnable case
			 * below can handle it for us anyway with less code. */
			printf("about to switch threads %d -> %d\n", old_tid,
			       new_tid);
			s->context_switch_pending = true;
			s->context_switch_target = new_tid;
		}
	}

	int target_tid;

	/* Update scheduler state. */
	if (kern_timer_entering(ls)) {
		/* TODO: would it be right to assert !handling_timer? */
		ACTION(s, handling_timer) = true;
	} else if (kern_timer_exiting(ls)) {
		assert(ACTION(s, handling_timer));
		ACTION(s, handling_timer) = false;
	} else if (kern_forking(ls)) {
		assert(!ACTION(s, handling_timer));
		assert(!ACTION(s, forking));
		assert(!ACTION(s, sleeping));
		assert(!ACTION(s, vanishing));
		ACTION(s, forking) = true;
	} else if (kern_sleeping(ls)) {
		assert(!ACTION(s, handling_timer));
		assert(!ACTION(s, forking));
		assert(!ACTION(s, sleeping));
		assert(!ACTION(s, vanishing));
		ACTION(s, sleeping) = true;
	} else if (kern_vanishing(ls)) {
		assert(!ACTION(s, handling_timer));
		assert(!ACTION(s, forking));
		assert(!ACTION(s, sleeping));
		assert(!ACTION(s, vanishing));
		ACTION(s, vanishing) = true;
	} else if (kern_thread_runnable(ls, &target_tid)) {
		/* A thread is about to become runnable. Was it just spawned? */
		if (ACTION(s, forking) && !HANDLING_INTERRUPT(s)) {
			printf("agent %d forked -- ", target_tid);
			agent_fork(s, target_tid);
			/* don't need this flag anymore; fork only forks once */
			ACTION(s, forking) = false;
		} else {
			printf("agent %d wake -- ", target_tid);
			agent_wake(s, target_tid);
		}
		print_qs(s);
		printf("\n");
		/* If this is happening from the context switcher, we also need
		 * to update the currently-running thread. */
		if (s->context_switch_pending) {
			assert(s->context_switch_target == target_tid);
			s->cur_agent = agent_by_tid(&s->rq, target_tid);
			s->context_switch_pending = false;
		}
	} else if (kern_thread_descheduling(ls, &target_tid)) {
		/* A thread is about to deschedule. Is it vanishing? */
		if (ACTION(s, vanishing) && !HANDLING_INTERRUPT(s)) {
			assert(s->cur_agent->tid == target_tid);
			agent_vanish(s, target_tid);
			printf("agent %d vanished -- ", target_tid);
			/* Future actions by this thread, such as scheduling
			 * somebody else, shouldn't count as them vanishing too! */
			ACTION(s, vanishing) = false;
		} else if (ACTION(s, sleeping) && !HANDLING_INTERRUPT(s)) {
			assert(s->cur_agent->tid == target_tid);
			agent_sleep(s, target_tid);
			printf("agent %d sleep -- ", target_tid);
			ACTION(s, sleeping) = false;
		} else {
			agent_deschedule(s, target_tid);
			printf("agent %d desch -- ", target_tid);
		}
		print_qs(s);
		printf("\n");
	}

	/* TODO: check for cli or sched locked before invoking timer */
}
