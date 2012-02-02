/**
 * @file schedule.c
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#include <simics/api.h>
#include <simics/alloc.h>

#define MODULE_NAME "SCHEDULE"
#define MODULE_COLOUR COLOUR_GREEN

#include "arbiter.h"
#include "common.h"
#include "found_a_bug.h"
#include "landslide.h"
#include "kernel_specifics.h"
#include "memory.h"
#include "schedule.h"
#include "variable_queue.h"
#include "x86.h"

/******************************************************************************
 * Agence
 ******************************************************************************/

struct agent *agent_by_tid_or_null(struct agent_q *q, int tid)
{
	struct agent *a;
	Q_SEARCH(a, q, nobe, a->tid == tid);
	return a;
}

struct agent *agent_by_tid(struct agent_q *q, int tid)
{
	struct agent *a = agent_by_tid_or_null(q, tid);
	assert(a && "attempt to look up tid not in specified queue");
	return a;
}

/* Call with whether or not the thread is created with a context-switch frame
 * crafted on its stack. Most threads would be; "init" may not be. */
static void agent_fork(struct sched_state *s, int tid, bool context_switch)
{
	struct agent *a = MM_MALLOC(1, struct agent);
	assert(a && "failed to allocate new agent");
	
	a->tid = tid;
	a->action.handling_timer = false;
	/* XXX: may not be true in some kernels; also a huge hack */
	a->action.context_switch = context_switch;
	a->action.forking = false; /* XXX: may not be true in some kernels */
	a->action.sleeping = false;
	a->action.vanishing = false;
	a->action.readlining = false;
	a->action.mutex_locking = false;
	a->action.mutex_unlocking = false;
	a->action.schedule_target = false;
	a->blocked_on = NULL;
	a->blocked_on_tid = -1;
	a->blocked_on_addr = -1;
	Q_INSERT_FRONT(&s->rq, a, nobe);

	s->num_agents++;
	if (s->num_agents > s->most_agents_ever) {
		s->most_agents_ever = s->num_agents;
	}
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
	assert(a == s->cur_agent);
	/* It turns out kernels tend to have vanished threads continue to be the
	 * "current thread" after our trigger point. It's only safe to free them
	 * after somebody else gets scheduled. */
	if (s->last_vanished_agent) {
		assert(!s->last_vanished_agent->action.handling_timer);
		assert(s->last_vanished_agent->action.context_switch);
		MM_FREE(s->last_vanished_agent);
	}
	s->last_vanished_agent = a;
	s->num_agents--;
}

static void set_schedule_target(struct sched_state *s, struct agent *a)
{
	if (s->schedule_in_flight != NULL) {
		lsprintf("warning: overriding old schedule target ");
		print_agent(s->schedule_in_flight);
		printf(" with new ");
		print_agent(a);
		printf("\n");
		s->schedule_in_flight->action.schedule_target = false;
	}
	s->schedule_in_flight = a;
	a->action.schedule_target = true;
}

static struct agent *get_blocked_on(struct sched_state *s, struct agent *src)
{
	if (src->blocked_on != NULL) {
		return src->blocked_on;
	} else {
		int tid = src->blocked_on_tid;
		struct agent *dest     = agent_by_tid_or_null(&s->rq, tid);
		if (dest == NULL) dest = agent_by_tid_or_null(&s->dq, tid);
		if (dest == NULL) dest = agent_by_tid_or_null(&s->sq, tid);
		/* Could still be null. */
		src->blocked_on = dest;
		return dest;
	}
}

static bool deadlocked(struct sched_state *s)
{
	struct agent *tortoise = s->cur_agent;
	struct agent *rabbit = get_blocked_on(s, tortoise);

	while (rabbit != NULL) {
		if (rabbit == tortoise) {
			return true;
		}
		tortoise = get_blocked_on(s, tortoise);
		rabbit = get_blocked_on(s, rabbit);
		if (rabbit != NULL) {
			rabbit = get_blocked_on(s, rabbit);
		}
	}
	return false;
}

static void print_deadlock(struct agent *a)
{
	struct agent *start = a;
	printf("(%d", a->tid);
	for (a = a->blocked_on; a != start; a = a->blocked_on) {
		assert(a != NULL && "a wasn't deadlocked!");
		printf(" -> %d", a->tid);
	}
	printf(" -> %d)", a->tid);
}

static void mutex_block_others(struct agent_q *q, int mutex_addr,
			       struct agent *blocked_on, int blocked_on_tid)
{
	struct agent *a;
	Q_FOREACH(a, q, nobe) {
		if (a->blocked_on_addr == mutex_addr) {
			lsprintf("mutex: on 0x%x tid %d now blocks on %d "
				 "(was %d)\n", mutex_addr, a->tid,
				 blocked_on_tid, a->blocked_on_tid);
			assert(a->action.mutex_locking);
			a->blocked_on = blocked_on;
			a->blocked_on_tid = blocked_on_tid;
		}
	}
}

/******************************************************************************
 * Scheduler
 ******************************************************************************/

void sched_init(struct sched_state *s)
{
	Q_INIT_HEAD(&s->rq);
	Q_INIT_HEAD(&s->dq);
	s->num_agents = 0;
	s->most_agents_ever = 0;
	kern_init_runqueue(s, agent_fork);
	s->cur_agent = agent_by_tid(&s->rq, kern_get_first_tid());
	s->last_agent = NULL;
	s->context_switch_pending = false;
	s->context_switch_target = 0xdeadd00d; /* poison value */
	s->last_vanished_agent = NULL;
	s->guest_init_done = false;
	s->schedule_in_flight = NULL;
	s->entering_timer = false;
}

void print_agent(struct agent *a)
{
	printf("%d", a->tid);
	if (a->action.handling_timer)  printf("t");
	if (a->action.context_switch)  printf("c");
	if (a->action.forking)         printf("f");
	if (a->action.sleeping)        printf("s");
	if (a->action.vanishing)       printf("v");
	if (a->action.readlining)      printf("r");
	if (a->action.schedule_target) printf("*");
	if (BLOCKED(a))                printf("<?%d>", a->blocked_on_tid);
}

void print_q(const char *start, struct agent_q *q, const char *end)
{
	struct agent *a;
	bool first = true;

	printf("%s", start);
	Q_FOREACH(a, q, nobe) {
		if (first)
			first = false;
		else
			printf(", ");
		print_agent(a);
	}
	printf("%s", end);
}
void print_qs(struct sched_state *s)
{
	printf("current ");
	print_agent(s->cur_agent);
	printf(" ");
	print_q(" RQ [", &s->rq, "] ");
	print_q(" SQ {", &s->sq, "} ");
	print_q(" DQ (", &s->dq, ") ");
}

/* what is the current thread doing? */
#define ACTION(s, act) ((s)->cur_agent->action.act)
#define HANDLING_INTERRUPT(s) (ACTION(s, handling_timer)) /* FIXME: add kbd */
#define NO_ACTION(s) (!(ACTION(s, handling_timer) || ACTION(s, context_switch) \
			|| ACTION(s, forking) || ACTION(s, sleeping)           \
			|| ACTION(s, vanishing) || ACTION(s, readlining)))


void sched_update(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	int old_tid = s->cur_agent->tid;
	int new_tid = kern_get_current_tid(ls->cpu0);

	/* wait until the guest is ready */
	if (!s->guest_init_done) {
		if (kern_sched_init_done(ls->eip)) {
			s->guest_init_done = true;
			assert(old_tid == new_tid && "init tid mismatch");
		} else {
			return;
		}
	}

	/* The Importance of Being Assertive, A Trivial Style Guideline for
	 * Serious Programmers, by Ben Blum */
	if (s->entering_timer) {
		assert(ls->eip == kern_get_timer_wrap_begin() &&
		       "simics is a clown and tried to delay our interrupt :<");
		s->entering_timer = false;
	} else {
		if (ls->eip == kern_get_timer_wrap_begin()) {
		       lsprintf("a timer interrupt that wasn't ours....");
		       print_qs(s);
		       printf("\n");
		       assert(0);
		}
	}

	/**********************************************************************
	 * Update scheduler state.
	 **********************************************************************/

	if (old_tid != new_tid && !s->context_switch_pending) {
		/* Careful! On some kernels, the trigger for a new agent forking
		 * (where it first gets added to the RQ) may happen AFTER its
		 * tcb is set to be the currently running thread. This would
		 * cause this case to be reached before agent_fork() is called,
		 * so agent_by_tid would fail. Instead, we have an option to
		 * find it later. (see the kern_thread_runnable case below.) */
		struct agent *next = agent_by_tid_or_null(&s->rq, new_tid);
		if (next) {
			lsprintf("switched threads %d -> %d\n", old_tid,
				 new_tid);
			s->last_agent = s->cur_agent;
			s->cur_agent = next;
		} else {
			/* there is also a possibility of searching s->dq, to
			 * find the thread, but the kern_thread_runnable case
			 * below can handle it for us anyway with less code. */
			lsprintf("about to switch threads %d -> %d\n", old_tid,
				 new_tid);
			s->context_switch_pending = true;
			s->context_switch_target = new_tid;
		}
	}

	int target_tid;
	int mutex_addr;

	/* Timer interrupt handling. */
	if (kern_timer_entering(ls->eip)) {
		// XXX: same as the comment in the below condition.
		if (!kern_timer_exiting(READ_STACK(ls->cpu0, 0))) {
			assert(!ACTION(s, handling_timer));
		} else {
			lsprintf("WARNING: allowing a nested timer on tid %d's "
				 "stack\n", s->cur_agent->tid);
		}
		ACTION(s, handling_timer) = true;
		lsprintf("%d timer enter from 0x%x\n", s->cur_agent->tid,
		         (unsigned int)READ_STACK(ls->cpu0, 0));
	} else if (kern_timer_exiting(ls->eip)) {
		assert(ACTION(s, handling_timer));
		// XXX: This condition is a hack to compensate for when simics
		// "sometimes", when keeping a schedule-in-flight, takes the
		// caused timer interrupt immediately, even before the iret.
		if (!kern_timer_exiting(READ_STACK(ls->cpu0, 0))) {
			ACTION(s, handling_timer) = false;
		}
		/* If the schedule target was in a timer interrupt when we
		 * decided to schedule him, then now is when the operation
		 * finishes landing. (otherwise, see below) */
		if (ACTION(s, schedule_target)) {
			ACTION(s, schedule_target) = false;
			s->schedule_in_flight = NULL;
		}
	/* Context switching. */
	} else if (kern_context_switch_entering(ls->eip)) {
		/* It -is- possible for a context switch to interrupt a
		 * context switch if a timer goes off before c-s disables
		 * interrupts. TODO: if we care, make this an int counter. */
		ACTION(s, context_switch) = true;
	} else if (kern_context_switch_exiting(ls->eip)) {
		assert(ACTION(s, context_switch));
		ACTION(s, context_switch) = false;
		/* For threads that context switched of their own accord. */
		if (ACTION(s, schedule_target) && !HANDLING_INTERRUPT(s)) {
			ACTION(s, schedule_target) = false;
			s->schedule_in_flight = NULL;
		}
	/* Lifecycle. */
	} else if (kern_forking(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, forking) = true;
	} else if (kern_sleeping(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, sleeping) = true;
	} else if (kern_vanishing(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, vanishing) = true;
	} else if (kern_readline_enter(ls->eip)) {
		assert(NO_ACTION(s));
		ACTION(s, readlining) = true;
	} else if (kern_readline_exit(ls->eip)) {
		assert(ACTION(s, readlining));
		ACTION(s, readlining) = false;
	/* Runnable state change (incl. consequences of fork, vanish, sleep). */
	} else if (kern_thread_runnable(ls->cpu0, ls->eip, &target_tid)) {
		/* A thread is about to become runnable. Was it just spawned? */
		if (ACTION(s, forking) && !HANDLING_INTERRUPT(s)) {
			lsprintf("agent %d forked -- ", target_tid);
			print_qs(s);
			printf("\n");
			agent_fork(s, target_tid, kern_fork_returns_to_cs());
			/* don't need this flag anymore; fork only forks once */
			ACTION(s, forking) = false;
		} else {
			// lsprintf("agent %d wake -- ", target_tid);
			agent_wake(s, target_tid);
		}
		/* If this is happening from the context switcher, we also need
		 * to update the currently-running thread. */
		if (s->context_switch_pending) {
			assert(s->context_switch_target == target_tid);
			s->last_agent = s->cur_agent;
			s->cur_agent = agent_by_tid(&s->rq, target_tid);
			s->context_switch_pending = false;
		}
	} else if (kern_thread_descheduling(ls->cpu0, ls->eip, &target_tid)) {
		/* A thread is about to deschedule. Is it vanishing? */
		if (ACTION(s, vanishing) && !HANDLING_INTERRUPT(s)) {
			assert(s->cur_agent->tid == target_tid);
			agent_vanish(s, target_tid);
			lsprintf("agent %d vanished -- ", target_tid);
			print_qs(s);
			printf("\n");
			/* Future actions by this thread, such as scheduling
			 * somebody else, shouldn't count as them vanishing too! */
			ACTION(s, vanishing) = false;
		} else if (ACTION(s, sleeping) && !HANDLING_INTERRUPT(s)) {
			assert(s->cur_agent->tid == target_tid);
			agent_sleep(s, target_tid);
			lsprintf("agent %d sleep -- ", target_tid);
			print_qs(s);
			printf("\n");
			ACTION(s, sleeping) = false;
		} else {
			agent_deschedule(s, target_tid);
			// lsprintf("agent %d desch -- ", target_tid);
		}
	/* Mutex tracking and noob deadlock detection */
	} else if (kern_mutex_locking(ls->cpu0, ls->eip, &mutex_addr)) {
		assert(!ACTION(s, mutex_locking));
		assert(!ACTION(s, mutex_unlocking));
		ACTION(s, mutex_locking) = true;
		s->cur_agent->blocked_on_addr = mutex_addr;
	} else if (kern_mutex_blocking(ls->cpu0, ls->eip, &target_tid)) {
		/* Possibly not the case - if this thread entered mutex_lock,
		 * then switched and someone took it, these would be set already
		 * assert(s->cur_agent->blocked_on == NULL);
		 * assert(s->cur_agent->blocked_on_tid == -1); */
		lsprintf("mutex: on 0x%x tid %d blocks, owned by %d\n",
			 s->cur_agent->blocked_on_addr, s->cur_agent->tid,
			 target_tid);
		s->cur_agent->blocked_on_tid = target_tid;
		if (deadlocked(s)) {
			lsprintf("DEADLOCK! ");
			print_deadlock(s->cur_agent);
			printf("\n");
			found_a_bug(ls);
		}
	} else if (kern_mutex_locking_done(ls->eip)) {
		assert(ACTION(s, mutex_locking));
		assert(!ACTION(s, mutex_unlocking));
		ACTION(s, mutex_locking) = false;
		s->cur_agent->blocked_on = NULL;
		s->cur_agent->blocked_on_tid = -1;
		s->cur_agent->blocked_on_addr = -1;
		/* no need to check for deadlock; this can't create a cycle. */
		mutex_block_others(&s->rq, mutex_addr, s->cur_agent,
				   s->cur_agent->tid);
	} else if (kern_mutex_unlocking(ls->cpu0, ls->eip, &mutex_addr)) {
		/* It's allowed to have a mutex_unlock call inside a mutex_lock
		 * (and it can happen), but not the other way around. */
		assert(!ACTION(s, mutex_unlocking));
		ACTION(s, mutex_unlocking) = true;
		mutex_block_others(&s->rq, mutex_addr, NULL, -1);
	} else if (kern_mutex_unlocking_done(ls->eip)) {
		assert(ACTION(s, mutex_unlocking));
		ACTION(s, mutex_unlocking) = false;
	/* Dynamic memory allocation tracking */
	} else {
		int size;
		int base;

		if (kern_lmm_alloc_entering(ls->cpu0, ls->eip, &size)) {
			mem_enter_bad_place(ls, &ls->mem, size);
		} else if (kern_lmm_alloc_exiting(ls->cpu0, ls->eip, &base)) {
			mem_exit_bad_place(ls, &ls->mem, base);
		} else if (kern_lmm_free_entering(ls->cpu0, ls->eip, &base, &size)) {
			mem_enter_free(ls, &ls->mem, base, size);
		} else if (kern_lmm_free_exiting(ls->eip)) {
			mem_exit_free(ls, &ls->mem);
		}
	}

	/**********************************************************************
	 * Exercise our will upon the guest kernel
	 **********************************************************************/

	/* Some checks before invoking the arbiter. First see if an operation of
	 * ours is already in-flight. */
	if (s->schedule_in_flight) {
		if (s->schedule_in_flight == s->cur_agent) {
			/* the in-flight schedule operation is cleared for
			 * landing. note that this may cause another one to
			 * be triggered again as soon as the context switcher
			 * and/or the timer handler finishes; it is up to the
			 * arbiter to decide this. */
			assert(ACTION(s, schedule_target));
			/* this condition should trigger in the middle of the
			 * switch, rather than after it finishes. (which is also
			 * why we leave the schedule_target flag turned on. */
			assert(ACTION(s, context_switch) ||
			       HANDLING_INTERRUPT(s));
			/* The schedule_in_flight flag itself is cleared above,
			 * along with schedule_target. Sometimes sched_recover
			 * sets in_flight and needs it not cleared here. */
		} else {
			/* An undesirable thread has been context-switched away
			 * from either from an interrupt handler (timer/kbd) or
			 * of its own accord. We need to wait for it to get back
			 * to its own execution before triggering an interrupt
			 * on it; in the former case, this will be just after it
			 * irets; in the latter, just after the c-s returns. */
			if (kern_timer_exiting(ls->eip) ||
			    (!HANDLING_INTERRUPT(s) &&
			     kern_context_switch_exiting(ls->eip))) {
				/* an undesirable agent just got switched to;
				 * keep the pending schedule in the air. */
				// XXX: this seems to get taken too soon? change
				// it somehow to cause_.._immediately. and then
				// see the asserts/comments in the action
				// handling_timer sections above.
				lsprintf("keeping schedule in-flight at 0x%x\n",
					 ls->eip);
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
			} else {
				/* they'd better not have "escaped" */
				assert(ACTION(s, context_switch) ||
				       HANDLING_INTERRUPT(s));
			}
		}
		/* in any case we have no more decisions to make here */
		return;
	}
	assert(!s->schedule_in_flight);

	/* Can't do anything before the test actually starts. */
	if (ls->test.current_test == NULL) {
		return;
	}

	/* XXX TODO: This will "leak" an undesirable thread to execute an
	 * instruction if the timer/kbd handler is an interrupt gate, so check
	 * also if we're about to iret and then examine the eflags on the
	 * stack. Also, "sti" and "popf" are interesting, so check for those.
	 * Also, do trap gates enable interrupts if they were off? o_O */
	if (!interrupts_enabled(ls->cpu0)) {
		return;
	}

	/* If a schedule operation is just finishing, we should allow the thread
	 * to get back to its own execution before making another choice. Note 
	 * that when we previously decided to interrupt the thread, it will have
	 * executed the single instruction we made the choice at then taken the
	 * interrupt, so we return to the next instruction, not the same one. */
	if (ACTION(s, schedule_target)) {
		return;
	}

	/* TODO: have an extra mode which will allow us to preempt the timer
	 * handler. */
	if (HANDLING_INTERRUPT(s) || kern_scheduler_locked(ls->cpu0)) {
		return;
	}

	/* As kernel_specifics.h says, no preempting during mutex unblocking. */
	if (ACTION(s, mutex_unlocking)) {
		return;
	}

	/* Okay, are we at a choice point? */
	/* TODO: arbiter may also want to see the trace_entry_t */
	if (arbiter_interested(ls)) {
		struct agent *a;
		bool our_choice;
		/* TODO: as an optimisation (in serialisation state / etc), the
		 * arbiter may return NULL if there was only one possible
		 * choice. */
		if (arbiter_choose(ls, &a, &our_choice)) {
			/* Effect the choice that was made... */
			if (a != s->cur_agent) {
				lsprintf("from agent %d, arbiter chose %d at "
					 "0x%x (called at 0x%x)\n",
					 s->cur_agent->tid, a->tid, ls->eip,
					 (unsigned int)READ_STACK(ls->cpu0, 0));
				set_schedule_target(s, a);
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
			}
			/* Record the choice that was just made. */
			save_setjmp(&ls->save, ls, a->tid, our_choice, false);
		} else {
			lsprintf("no agent was chosen at eip 0x%x\n", ls->eip);
		}
	}
	/* XXX TODO: it may be that not every timer interrupt triggers a context
	 * switch, so we should watch out if a handler doesn't enter the c-s. */
}

void sched_recover(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	int tid;

	assert(ls->just_jumped);

	if (arbiter_pop_choice(&ls->arbiter, &tid)) {
		if (tid == s->cur_agent->tid) {
			/* Hmmmm */
			if (kern_timer_entering(ls->eip)) {
				/* Oops, we ended up trying to leave the thread
				 * we want to be running. Make sure to go
				 * back... */
				set_schedule_target(s, s->cur_agent);
				assert(s->entering_timer);
				lsprintf("Explorer-chosen tid %d wants to run; "
					 "not switching away\n", tid);
				/* Make sure the arbiter knows this isn't a
				 * voluntary reschedule. The handling_timer flag
				 * won't be on now, but sched_update sets it. */
				lsprintf("Updating the last_agent: ");
				print_agent(s->last_agent);
				printf(" to ");
				print_agent(s->cur_agent);
				printf("\n");
				s->last_agent = s->cur_agent;
			} else {
				lsprintf("Explorer-chosen tid %d already "
					 "running!\n", tid);
			}
		} else {
			// TODO: duplicate agent search logic (arbiter.c)
			struct agent *a = agent_by_tid_or_null(&s->rq, tid);
			if (a == NULL) {
				a = agent_by_tid_or_null(&s->sq, tid);
			}

			assert(a != NULL && "bogus explorer-chosen tid!");
			lsprintf("Recovering to explorer-chosen tid %d from "
				 "tid %d\n", tid, s->cur_agent->tid);
			set_schedule_target(s, a);
			/* Hmmmm */
			if (!kern_timer_entering(ls->eip)) {
				ls->eip = cause_timer_interrupt_immediately(
					ls->cpu0);
			}
			s->entering_timer = true;
		}
	} else {
		tid = s->cur_agent->tid;
		lsprintf("Explorer chose no tid; defaulting to %d\n", tid);
	}

	save_recover(&ls->save, ls, tid);
}
