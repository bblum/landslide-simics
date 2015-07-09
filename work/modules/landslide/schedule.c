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
#include "kspec.h"
#include "memory.h"
#include "schedule.h"
#include "stack.h"
#include "user_specifics.h"
#include "variable_queue.h"
#include "x86.h"

static void print_q(verbosity v, const char *start, const struct agent_q *q,
		    const char *end, unsigned int dont_print_tid);

/******************************************************************************
 * Agence
 ******************************************************************************/

struct agent *agent_by_tid_or_null(struct agent_q *q, unsigned int tid)
{
	struct agent *a;
	Q_SEARCH(a, q, nobe, a->tid == tid);
	return a;
}

#define agent_by_tid(q, tid) _agent_by_tid(q, tid, #q)
static struct agent *_agent_by_tid(struct agent_q *q, unsigned int tid, const char *q_name)
{
	struct agent *a = agent_by_tid_or_null(q, tid);
	if (a == NULL) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "TID %d isn't in the "
			 "right queue (expected: %s); probably incorrect "
			 "annotations?\n", tid, q_name);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Queue contains: ");
		print_q(ALWAYS, "", q, "", -1);
		printf(ALWAYS, "\n");
		LS_ABORT();
	}
	return a;
}

/* Call with whether or not the thread is created with a context-switch frame
 * crafted on its stack. Most threads would be; "init" may not be. */
static void agent_fork(struct sched_state *s, unsigned int tid, bool on_runqueue)
{
	struct agent *a = MM_XMALLOC(1, struct agent);

	a->tid = tid;
	a->action.handling_timer = false;
	/* this is usually not true, but makes it easier on the student; see
	 * the free pass below. */
	a->action.context_switch = false;
	/* If being called from kern_init_threads, don't give the free pass. */
	a->action.cs_free_pass = true;
	a->action.forking = false;
	a->action.sleeping = false;
	a->action.vanishing = false;
	a->action.readlining = false;
	a->action.just_forked = true;
	a->action.lmm_init = false;
	a->action.vm_user_copy = false;
	a->action.disk_io = false;
	a->action.kern_mutex_locking = false;
	a->action.kern_mutex_unlocking = false;
	a->action.kern_mutex_trylocking = false;
	a->action.user_mutex_initing = false;
	a->action.user_mutex_locking = false;
	a->action.user_mutex_unlocking = false;
	a->action.user_mutex_yielding = false;
	a->action.user_mutex_destroying = false;
	a->action.user_cond_waiting = false;
	a->action.user_cond_signalling = false;
	a->action.user_cond_broadcasting = false;
	a->action.user_sem_proberen = false;
	a->action.user_sem_verhogen = false;
	a->action.user_rwlock_locking = false;
	a->action.user_rwlock_unlocking = false;
	a->action.user_locked_mallocing = false;
	a->action.user_locked_callocing = false;
	a->action.user_locked_reallocing = false;
	a->action.user_locked_freeing = false;
	a->action.schedule_target = false;
	a->kern_blocked_on = NULL;
	a->kern_blocked_on_tid = -1;
	a->kern_blocked_on_addr = -1;
	a->kern_mutex_unlocking_addr = -1;
	a->user_blocked_on_addr = -1;
	a->user_mutex_initing_addr = -1;
	a->user_mutex_locking_addr = -1;
	a->user_mutex_unlocking_addr = -1;
	a->user_rwlock_locking_addr = -1;
	a->just_delayed_for_data_race = false;
	a->delayed_data_race_eip = -1;
	a->most_recent_syscall = 0;

	lockset_init(&a->kern_locks_held);
	lockset_init(&a->user_locks_held);

	user_yield_state_init(&a->user_yield);

	a->pre_vanish_trace = NULL;

	if (on_runqueue) {
		Q_INSERT_FRONT(&s->rq, a, nobe);
	} else {
		Q_INSERT_FRONT(&s->dq, a, nobe);
	}

	s->num_agents++;
	if (s->num_agents > s->most_agents_ever) {
		s->most_agents_ever = s->num_agents;
	}
}

static void agent_wake(struct sched_state *s, unsigned int tid)
{
	struct agent *a = agent_by_tid_or_null(&s->rq, tid);
	if (a) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "tell_landslide_on_rq"
			 "(TID %d) called, but that thread is already on the"
			 "runqueue! Probably incorrect annotations?\n", tid);
		LS_ABORT();
	}
	a = agent_by_tid_or_null(&s->dq, tid);
	if (a) {
		Q_REMOVE(&s->dq, a, nobe);
	} else {
		a = agent_by_tid(&s->sq, tid);
		Q_REMOVE(&s->sq, a, nobe);
	}
	Q_INSERT_FRONT(&s->rq, a, nobe);
}

static void agent_deschedule(struct sched_state *s, unsigned int tid)
{
	struct agent *a = agent_by_tid_or_null(&s->rq, tid);
	if (a != NULL) {
		Q_REMOVE(&s->rq, a, nobe);
		Q_INSERT_FRONT(&s->dq, a, nobe);
	/* If it's not on the runqueue, we must have already special-case moved
	 * it off in the thread-change event. */
	} else if (agent_by_tid_or_null(&s->sq, tid) == NULL) {
		/* Either it's on the sleep queue, or it vanished. */
		if (agent_by_tid_or_null(&s->dq, tid) != NULL) {
			lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "TID %d is "
				 "already off the runqueue at tell_off_rq(); "
				 "probably incorrect annotations?\n", tid);
			LS_ABORT();
		}
	}
}

static void current_dequeue(struct sched_state *s)
{
	struct agent *a = agent_by_tid_or_null(&s->rq, s->cur_agent->tid);
	if (a == NULL) {
		a = agent_by_tid(&s->dq, s->cur_agent->tid);
		Q_REMOVE(&s->dq, a, nobe);
	} else {
		Q_REMOVE(&s->rq, a, nobe);
	}
	assert(a == s->cur_agent);
}

static void agent_sleep(struct sched_state *s)
{
	current_dequeue(s);
	Q_INSERT_FRONT(&s->sq, s->cur_agent, nobe);
}

static void agent_vanish(struct sched_state *s)
{
	current_dequeue(s);
	/* It turns out kernels tend to have vanished threads continue to be the
	 * "current thread" after our trigger point. It's only safe to free them
	 * after somebody else gets scheduled. */
	if (s->last_vanished_agent) {
		assert(!s->last_vanished_agent->action.handling_timer);
		assert(s->last_vanished_agent->action.context_switch);
		lockset_free(&s->last_vanished_agent->kern_locks_held);
		lockset_free(&s->last_vanished_agent->user_locks_held);
		if (s->last_vanished_agent->pre_vanish_trace != NULL) {
			free_stack_trace(s->last_vanished_agent->pre_vanish_trace);
		}
		MM_FREE(s->last_vanished_agent);
	}
	s->last_vanished_agent = s->cur_agent;
	s->num_agents--;
}

static void set_schedule_target(struct sched_state *s, struct agent *a)
{
	if (s->schedule_in_flight != NULL) {
		lsprintf(INFO, "warning: overriding old schedule target ");
		print_agent(INFO, s->schedule_in_flight);
		printf(INFO, " with new ");
		print_agent(INFO, a);
		printf(INFO, "\n");
		s->schedule_in_flight->action.schedule_target = false;
	}
	s->schedule_in_flight = a;
	a->action.schedule_target = true;
}

static struct agent *get_blocked_on(struct sched_state *s, struct agent *src)
{
	if (src->kern_blocked_on != NULL) {
		return src->kern_blocked_on;
	} else {
		unsigned int tid = src->kern_blocked_on_tid;
		struct agent *dest     = agent_by_tid_or_null(&s->rq, tid);
		if (dest == NULL) dest = agent_by_tid_or_null(&s->dq, tid);
		if (dest == NULL) dest = agent_by_tid_or_null(&s->sq, tid);
		/* Could still be null. */
		src->kern_blocked_on = dest;
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

static unsigned int print_deadlock(char *buf, unsigned int len, struct agent *a)
{
	struct agent *start = a;
	unsigned int pos = scnprintf(buf, len, "(%d", a->tid);
	for (a = a->kern_blocked_on; a != start; a = a->kern_blocked_on) {
		assert(a != NULL && "a wasn't deadlocked!");
		pos += scnprintf(buf + pos, len - pos, " -> %d", a->tid);
	}
	pos += scnprintf(buf + pos, len - pos, " -> %d)", a->tid);
	return pos;
}

static void kern_mutex_block_others(struct agent_q *q, unsigned int mutex_addr,
			            struct agent *kern_blocked_on,
			            unsigned int kern_blocked_on_tid)
{
	// FIXME: Issue 167
#ifndef PINTOS_KERNEL
	struct agent *a;
	assert(mutex_addr != -1);
	Q_FOREACH(a, q, nobe) {
		if (a->kern_blocked_on_addr == mutex_addr) {
			lsprintf(DEV, "mutex: on 0x%x tid %d now blocks on %d "
				 "(was %d)\n", mutex_addr, a->tid,
				 kern_blocked_on_tid, a->kern_blocked_on_tid);
			assert(a->action.kern_mutex_locking);
			a->kern_blocked_on = kern_blocked_on;
			a->kern_blocked_on_tid = kern_blocked_on_tid;
		}
	}
#endif
}

static void user_mutex_block_others(struct agent_q *q, unsigned int mutex_addr, bool mutex_held)
{
	struct agent *a;
	assert(mutex_addr != -1);
	assert(mutex_addr != 0);
	assert(USER_MEMORY(mutex_addr));
	Q_FOREACH(a, q, nobe) {
		/* slightly different than for kernel mutexes, since we
		 * don't have the first tid a contender gets blocked on */
		if (a->action.user_mutex_yielding &&
		    a->user_mutex_locking_addr == mutex_addr) {
			if (mutex_held) {
				/* lock was taken; the contender becomes blocked. */
				a->user_blocked_on_addr = mutex_addr;
			} else {
				/* lock available; the contender becomes unblocked. */
				a->user_blocked_on_addr = -1;
			}
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
	Q_INIT_HEAD(&s->sq);
	s->num_agents = 0;
	s->most_agents_ever = 0;
	s->guest_init_done = false; /* must be before kern_init_threads */
	kern_init_threads(s, agent_fork);
	s->cur_agent = agent_by_tid_or_null(&s->rq, kern_get_first_tid());
	if (s->cur_agent == NULL)
		s->cur_agent = agent_by_tid(&s->dq, kern_get_first_tid());
	s->last_agent = NULL;
	s->last_vanished_agent = NULL;
	s->schedule_in_flight = NULL;
	s->inflight_tick_count = 0;
	s->delayed_in_flight = false;
	s->just_finished_reschedule = false;
	s->entering_timer = false;
	s->voluntary_resched_tid = -1;
	s->voluntary_resched_stack = NULL;
	lockset_init(&s->known_semaphores);
}

void print_agent(verbosity v, const struct agent *a)
{
	printf(v, "%d", a->tid);
	if (MAX_VERBOSITY >= DEV) {
		if (a->action.handling_timer)  printf(v, "t");
		if (a->action.context_switch)  printf(v, "c");
		if (a->action.forking)         printf(v, "f");
		if (a->action.sleeping)        printf(v, "s");
		if (a->action.vanishing)       printf(v, "v");
		if (a->action.readlining)      printf(v, "r");
		if (a->action.schedule_target) printf(v, "*");
	}
	if (BLOCKED(a)) {
		if (a->kern_blocked_on_tid != -1) {
			printf(v, "<blocked on %d>", a->kern_blocked_on_tid);
		} else if (a->user_blocked_on_addr != -1) {
			printf(v, "{blocked on %x}", a->user_blocked_on_addr);
		} else if (agent_is_user_yield_blocked(&a->user_yield)) {
			printf(v, "{yield loop}");
		} else {
			assert(false && "unknown blocked condition");
		}
	}
}

static void print_q(verbosity v, const char *start, const struct agent_q *q,
		    const char *end, unsigned int dont_print_tid)
{
	struct agent *a;
	bool first = true;

	printf(v, "%s", start);
	Q_FOREACH(a, q, nobe) {
		if (a->tid != dont_print_tid) {
			if (first)
				first = false;
			else
				printf(v, ", ");
			print_agent(v, a);
		}
	}
	printf(v, "%s", end);
}
void print_qs(verbosity v, const struct sched_state *s)
{
	printf(v, "current ");
	print_agent(v, s->cur_agent);
	printf(v, " ");
	print_q(v, " RQ [", &s->rq, "] ", -1);
	print_q(v, " SQ {", &s->sq, "} ", -1);
	print_q(v, " DQ (", &s->dq, ") ", -1);
}

/* like print_qs, but human-friendly. */
void print_scheduler_state(verbosity v, const struct sched_state *s)
{
	unsigned int dont_print_tid = -1;
	printf(v, "runnable ");
	if (s->current_extra_runnable) {
		print_agent(v, s->cur_agent);
		dont_print_tid = s->cur_agent->tid;
		if (Q_GET_SIZE(&s->rq) != 0) {
			printf(v, ", ");
		}
	}
	print_q(v, "", &s->rq, "", dont_print_tid);
	if (Q_GET_SIZE(&s->sq) != 0) {
		print_q(v, "; sleeping", &s->sq, "", dont_print_tid);
	}
	print_q(v, "; descheduled ", &s->dq, "", dont_print_tid);
}

struct agent *find_runnable_agent(struct sched_state *s, unsigned int tid)
{
	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, s,
		if (a->tid == tid) {
			return a;
		}
	);
	return NULL;
}

/******************************************************************************
 * Kernelspace lifecycle events
 ******************************************************************************/

/* what is the current thread doing? */
#define CURRENT(s, field) ((s)->cur_agent->field)
#define ACTION(s, act) CURRENT((s), action.act)
#define HANDLING_INTERRUPT(s) (ACTION(s, handling_timer)) /* FIXME: add kbd */
#define NO_ACTION(s) (!(ACTION(s, handling_timer) || ACTION(s, context_switch) \
			|| ACTION(s, forking) || ACTION(s, sleeping)           \
			|| ACTION(s, vanishing) || ACTION(s, readlining)))

#define CHECK_NOT_ACTION(failed, s, act) do {				\
	if (ACTION(s, act)) {						\
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Current thread '" \
		         #act "' is true but shouldn't be\n");		\
		failed = true;						\
	} } while (0)
/* A more user-friendly way of asserting NO_ACTION. */
static void assert_no_action(struct sched_state *s, const char *new_act)
{
	bool failed = false;
	CHECK_NOT_ACTION(failed, s, handling_timer);
	CHECK_NOT_ACTION(failed, s, context_switch);
	CHECK_NOT_ACTION(failed, s, forking);
	CHECK_NOT_ACTION(failed, s, sleeping);
	CHECK_NOT_ACTION(failed, s, vanishing);
	CHECK_NOT_ACTION(failed, s, readlining);
	if (failed) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "While trying to do %s;"
			 " probably incorrect annotations?\n", new_act);
		LS_ABORT();
	}
}
#undef CHECK_NOT_ACTION

static bool handle_fork(struct sched_state *s, unsigned int target_tid, bool add_to_rq)
{
	if (ACTION(s, forking) && !HANDLING_INTERRUPT(s) &&
	    CURRENT(s, tid) != target_tid) {
		lsprintf(DEV, "agent %d forked (%s) -- ", target_tid,
			  add_to_rq ? "rq" : "dq");
		print_qs(DEV, s);
		printf(DEV, "\n");
		/* Start all newly-forked threads not in the context switcher.
		 * The free pass gets them out of the first assertion on the
		 * cs state flag. Of note, this means we can't reliably use the
		 * cs state flag for anything other than assertions. */
		agent_fork(s, target_tid, add_to_rq);
		/* don't need this flag anymore; fork only forks once */
		ACTION(s, forking) = false;
		return true;
	} else {
		return false;
	}
}
static void handle_sleep(struct sched_state *s)
{
	if (ACTION(s, sleeping) && !HANDLING_INTERRUPT(s)) {
		lsprintf(DEV, "agent %d sleep -- ", CURRENT(s, tid));
		print_qs(DEV, s);
		printf(DEV, "\n");
		agent_sleep(s);
		/* it doesn't quite matter where this flag gets turned off, but
		 * there are two places where it can get woken up (wake/unsleep)
		 * so may as well do it here. */
		ACTION(s, sleeping) = false;
	}
}
static void handle_vanish(struct sched_state *s)
{
	if (ACTION(s, vanishing) && !HANDLING_INTERRUPT(s)) {
		lsprintf(DEV, "agent %d vanish -- ", CURRENT(s, tid));
		print_qs(DEV, s);
		printf(DEV, "\n");
		agent_vanish(s);
		/* the vanishing flag stays on (TODO: is it needed?) */
	}
}
static void handle_unsleep(struct sched_state *s, unsigned int tid)
{
	/* If a thread-change happens to an agent on the sleep queue, that means
	 * it has woken up but runnable() hasn't seen it yet. So put it on the
	 * dq, which will satisfy whether or not runnable() triggers. */
	struct agent *a = agent_by_tid_or_null(&s->sq, tid);
	if (a != NULL) {
		Q_REMOVE(&s->sq, a, nobe);
		Q_INSERT_FRONT(&s->dq, a, nobe);
	}
}

/******************************************************************************
 * Main entry point(s) and state machine logic
 ******************************************************************************/

static void sched_check_lmm_init(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;

	if (kern_lmm_init_entering(ls->eip)) {
		assert_no_action(s, "lmm remove free");
		ACTION(s, lmm_init) = true;
	} else if (kern_lmm_init_exiting(ls->eip)) {
		assert(ACTION(s, lmm_init));
		ACTION(s, lmm_init) = false;
	}
}

static void sched_check_pintos_init_sequence(struct ls_state *ls)
{
#ifdef PINTOS_KERNEL
	unsigned int lock_addr;
	bool is_sema;
	if (ls->eip == GUEST_TIMER_CALIBRATE) {
		if (write_memory(ls->cpu0, GUEST_TIMER_CALIBRATE_RESULT,
				  GUEST_TIMER_CALIBRATE_VALUE, WORD_SIZE)) {
			lskprintf(DEV, "Nice try pintos, but there will be "
				  "no timer calibration on my watch.\n");
			SET_CPU_ATTR(ls->cpu0, eip, GUEST_TIMER_CALIBRATE_END);
			ls->eip = GUEST_TIMER_CALIBRATE_END;
		} else {
			lskprintf(DEV, "Warning: Couldn't avoid timer "
				  "calibration routine. Just be patient!\n");
		}
	} else if (ls->eip == GUEST_TIMER_MSLEEP) {
		if (write_memory(ls->cpu0, GET_CPU_ATTR(ls->cpu0, esp)
				 + WORD_SIZE, 0, WORD_SIZE)) {
			lskprintf(DEV, "I tried to eat a clock once, "
				  "but it was very time-consuming.\n");
		} else {
			lskprintf(DEV, "Warning: Couldn't avoid timer_msleep. "
				  "Just be patient!\n");
		}
	} else if (kern_mutex_initing(ls->cpu0, ls->eip, &lock_addr, &is_sema)) {
		lockset_record_semaphore(&ls->sched.known_semaphores,
					 lock_addr, is_sema);
	}
#endif
}

static void sched_finish_inflight(struct sched_state *s)
{
	ACTION(s, schedule_target) = false;
	s->schedule_in_flight = NULL;
	s->inflight_tick_count = 0;
}

#define TOO_MANY_INTERRUPTS(s) ((s)->most_agents_ever * 100)

static void keep_schedule_inflight(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;

	cause_timer_interrupt(ls->cpu0);

	assert(s->schedule_in_flight != NULL);
	s->entering_timer = true;
	s->delayed_in_flight = false;
	s->inflight_tick_count++;
	if (s->inflight_tick_count == TOO_MANY_INTERRUPTS(s)) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Too many interrupts "
			 "while trying to force thread %d to run,\n",
			 s->schedule_in_flight->tid);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "without successfully "
			 "rescheduling to it. Something is wrong...\n");
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Scheduler state: ");
		print_qs(ALWAYS, s);
		printf(ALWAYS, COLOUR_DEFAULT "\n");
		LS_ABORT();
	}
}

static void sched_update_kern_state_machine(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	unsigned int target_tid;
	unsigned int lock_addr;
	bool succeeded;
	bool is_sema;

	/* Timer interrupt handling. */
	if (kern_timer_entering(ls->eip)) {
		// XXX: same as the comment in the below condition.
		if (!kern_timer_exiting(READ_STACK(ls->cpu0, 0))) {
			assert(!ACTION(s, handling_timer));
		} else {
			lskprintf(DEV, "WARNING: allowing a nested timer on "
			          "tid %d's stack\n", CURRENT(s, tid));
		}
		ACTION(s, handling_timer) = true;
		lskprintf(INFO, "%d timer enter from 0x%x\n", CURRENT(s, tid),
		          (unsigned int)READ_STACK(ls->cpu0, 0));
	} else if (kern_timer_exiting(ls->eip)) {
		if (ACTION(s, handling_timer)) {
			// XXX: This condition is a hack to compensate for when
			// simics "sometimes", when keeping a schedule-in-
			// flight, takes the caused timer interrupt immediately,
			// even before the iret.
			if (!kern_timer_exiting(READ_STACK(ls->cpu0, 0))) {
				ACTION(s, handling_timer) = false;
				s->just_finished_reschedule = true;
			}
			/* If the schedule target was in a timer interrupt when we
			 * decided to schedule him, then now is when the operation
			 * finishes landing. (otherwise, see below)
			 * FIXME: should this be inside the above if statement? */
			if (ACTION(s, schedule_target)) {
				sched_finish_inflight(s);
			}
		} else {
			lskprintf(INFO, "WARNING: exiting a non-timer interrupt "
			          "through a path shared with the timer..? (from 0x%x, #%d)\n", READ_STACK(ls->cpu0, 0), READ_STACK(ls->cpu0, -2));
		}
	/* Context switching. */
	} else if (kern_context_switch_entering(ls->eip)) {
		/* It -is- possible for a context switch to interrupt a
		 * context switch if a timer goes off before c-s disables
		 * interrupts. TODO: if we care, make this an int counter. */
		ACTION(s, context_switch) = true;
		lskprintf(DEV, "entering context switch (TID %d)\n", CURRENT(s, tid));
		/* Maybe update the voluntary resched trace. See schedule.h */
		if (!ACTION(s, handling_timer)) {
			lsprintf(DEV, "Voluntary resched tid ");
			print_agent(DEV, s->cur_agent);
			printf(DEV, "\n");
			s->voluntary_resched_tid = CURRENT(s, tid);
			if (s->voluntary_resched_stack != NULL)
				free_stack_trace(s->voluntary_resched_stack);
			s->voluntary_resched_stack = stack_trace(ls);
		}
		if (READ_BYTE(ls->cpu0, ls->eip) == OPCODE_INT) {
			/* I'm not actually sure if this is where an INT would
			 * happen in a hurdle-violationg context switcher...? */
			HURDLE_VIOLATION("The context switch entry path seems "
					 "to use INT!");
		}
	} else if (kern_context_switch_exiting(ls->eip)) {
		assert(ACTION(s, cs_free_pass) || ACTION(s, context_switch));
		ACTION(s, context_switch) = false;
		ACTION(s, cs_free_pass) = false;
		lskprintf(DEV, "exiting context switch (TID %d)\n", CURRENT(s, tid));
		/* the MZ memorial condition -- "some" context switchers might
		 * also return from the timer interrupt handler. Attempt to cope. */
		if (READ_BYTE(ls->cpu0, ls->eip) == OPCODE_IRET) {
			if (!kern_timer_exiting(READ_STACK(ls->cpu0, 0))) {
				ACTION(s, handling_timer) = false;
                                lskprintf(DEV, "JFR site #1b\n");
				s->just_finished_reschedule = true;
			}
			if (ACTION(s, schedule_target)) { /* as above */
				sched_finish_inflight(s);
			}
			/* But, it's also a hurdle violation... */
			HURDLE_VIOLATION("The context switch return path "
					 "seems to use IRET!");
		/* For threads that context switched of their own accord. */
		} else if (!HANDLING_INTERRUPT(s)) {
                        lskprintf(DEV, "JFR site #2\n");
			s->just_finished_reschedule = true;
			if (ACTION(s, schedule_target)) {
				sched_finish_inflight(s);
			}
		}
	/* Lifecycle. */
	} else if (kern_forking(ls->eip)) {
		assert_no_action(s, "forking");
		ACTION(s, forking) = true;
	} else if (kern_sleeping(ls->eip)) {
		assert_no_action(s, "sleeping");
		ACTION(s, sleeping) = true;
	} else if (kern_vanishing(ls->eip)) {
		assert_no_action(s, "vanishing");
		ACTION(s, vanishing) = true;
	} else if (kern_readline_enter(ls->eip)) {
		assert_no_action(s, "readlining");
		ACTION(s, readlining) = true;
	} else if (kern_readline_exit(ls->eip)) {
		assert(ACTION(s, readlining));
		ACTION(s, readlining) = false;
	/* Runnable state change (incl. consequences of fork, vanish, sleep). */
	} else if (kern_thread_runnable(ls->cpu0, ls->eip, &target_tid)) {
		/* A thread is about to become runnable. Was it just spawned? */
		if (!handle_fork(s, target_tid, true)) {
			agent_wake(s, target_tid);
		}
	} else if (kern_thread_descheduling(ls->cpu0, ls->eip, &target_tid)) {
		/* A thread is about to deschedule. Is it vanishing/sleeping? */
		agent_deschedule(s, target_tid);
	/* Mutex tracking and noob deadlock detection */
	} else if (kern_mutex_initing(ls->cpu0, ls->eip, &lock_addr, &is_sema)) {
		lockset_record_semaphore(&s->known_semaphores, lock_addr, is_sema);
	} else if (kern_mutex_locking(ls->cpu0, ls->eip, &lock_addr)) {
		//assert(!ACTION(s, kern_mutex_locking));
		assert(!ACTION(s, kern_mutex_unlocking));
		ACTION(s, kern_mutex_locking) = true;
		CURRENT(s, kern_blocked_on_addr) = lock_addr;
		lockset_add(s, &CURRENT(s, kern_locks_held), lock_addr, LOCK_MUTEX);
	} else if (kern_mutex_blocking(ls->cpu0, ls->eip, &target_tid)) {
		/* Possibly not the case - if this thread entered mutex_lock,
		 * then switched and someone took it, these would be set already
		 * assert(CURRENT(s, kern_blocked_on) == NULL);
		 * assert(CURRENT(s, kern_blocked_on_tid) == -1); */
		lskprintf(DEV, "mutex: on 0x%x tid %d blocks, owned by %d\n",
		          CURRENT(s, kern_blocked_on_addr), CURRENT(s, tid),
		          target_tid);
		CURRENT(s, kern_blocked_on_tid) = target_tid;
		// An odd interleaving can cause a contendingthread to become
		// unblocked before they run far enough to say they're blocked.
		// So if they were unblocked, they are not really blocked.
		if (CURRENT(s, kern_blocked_on_addr) == -1) {
			if (deadlocked(s)) {
				char buf[BUF_SIZE];
				unsigned int len =
					print_deadlock(buf, BUF_SIZE, s->cur_agent);
				FOUND_A_BUG(ls, "KERNEL DEADLOCK! %.*s", len, buf);
			}
		}
	} else if (kern_mutex_locking_done(ls->cpu0, ls->eip, &lock_addr)) {
		// XXX: Why is this assert commented out? I don't remember.
		//assert(ACTION(s, kern_mutex_locking));
		assert(!ACTION(s, kern_mutex_unlocking));
		ACTION(s, kern_mutex_locking) = false;
		lskprintf(DEV, "mutex: on 0x%x tid %d unblocks\n",
		          CURRENT(s, kern_blocked_on_addr), CURRENT(s, tid));
		CURRENT(s, kern_blocked_on) = NULL;
		CURRENT(s, kern_blocked_on_tid) = -1;
		CURRENT(s, kern_blocked_on_addr) = -1;
		/* no need to check for deadlock; this can't create a cycle. */
		kern_mutex_block_others(&s->rq, lock_addr, s->cur_agent,
					CURRENT(s, tid));
	} else if (kern_mutex_trylocking(ls->cpu0, ls->eip, &lock_addr)) {
		/* trylocking can happen in the timer handler, so it is expected
		 * to preempt a lock or unlock operation. */
		// XXX: couldn't it also preempt itself, with a PP on trylock?
		assert(!ACTION(s, kern_mutex_trylocking));
		ACTION(s, kern_mutex_trylocking) = true;
		lockset_add(s, &CURRENT(s, kern_locks_held), lock_addr, LOCK_MUTEX);
	} else if (kern_mutex_trylocking_done(ls->cpu0, ls->eip, &lock_addr, &succeeded)) {
		assert(ACTION(s, kern_mutex_trylocking));
		assert(!ACTION(s, kern_mutex_unlocking));
		ACTION(s, kern_mutex_trylocking) = false;
		if (succeeded) {
			/* similar to mutex_locking_done case */
			kern_mutex_block_others(&s->rq, lock_addr, s->cur_agent,
						CURRENT(s, tid));
		} else {
			/* simply undo changes from trylock begin */
			lockset_remove(s, lock_addr, LOCK_MUTEX, true);
		}
	} else if (kern_mutex_unlocking(ls->cpu0, ls->eip, &lock_addr)) {
		/* It's allowed to have a mutex_unlock call inside a mutex_lock
		 * (and it can happen), or mutex_lock inside of mutex_lock, but
		 * not the other way around. */
		assert(!ACTION(s, kern_mutex_unlocking));
		ACTION(s, kern_mutex_unlocking) = true;
		assert(CURRENT(s, kern_mutex_unlocking_addr) == -1);
		CURRENT(s, kern_mutex_unlocking_addr) = lock_addr;
		lskprintf(DEV, "mutex: 0x%x unlocked by tid %d\n",
		          lock_addr, CURRENT(s, tid));
		kern_mutex_block_others(&s->rq, lock_addr, NULL, -1);
	} else if (kern_mutex_unlocking_done(ls->eip)) {
		assert(ACTION(s, kern_mutex_unlocking));
		ACTION(s, kern_mutex_unlocking) = false;
		assert(CURRENT(s, kern_mutex_unlocking_addr) != -1);
		lockset_remove(s, CURRENT(s, kern_mutex_unlocking_addr),
			       LOCK_MUTEX, true);
		CURRENT(s, kern_mutex_unlocking_addr) = -1;
		lskprintf(DEV, "mutex: unlocking done by tid %d\n",
			  CURRENT(s, tid));
	/* Etc. */
	} else if (kern_wants_us_to_dump_stack(ls->eip)) {
		dump_stack();
	} else if (kern_vm_user_copy_enter(ls->eip)) {
		assert(!ACTION(s, vm_user_copy));
		ACTION(s, vm_user_copy) = true;
	} else if (kern_vm_user_copy_exit(ls->eip)) {
		assert(ACTION(s, vm_user_copy));
		ACTION(s, vm_user_copy) = false;
	} else if (kern_beginning_vanish_before_unreg_process(ls->eip)) {
		assert(CURRENT(s, pre_vanish_trace) == NULL);
		CURRENT(s, pre_vanish_trace) = stack_trace(ls);
	} else if (kern_enter_disk_io_fn(ls->eip)) {
		/* Functions declared with disk_io_fn aren't allowed to call
		 * each other, for simplicity of implementation. */
		assert(!ACTION(s, disk_io) && "(co)recursive disk_io not supported");
		ACTION(s, disk_io) = true;
	} else if (kern_exit_disk_io_fn(ls->eip)) {
		assert(ACTION(s, disk_io) && "(co)recursive disk_io not supported");
		ACTION(s, disk_io) = false;
	} else {
		sched_check_lmm_init(ls);
	}
}

static void sched_update_user_state_machine(struct ls_state *ls)
{
	/* Tracking events in userspace. One thing to note about this section
	 * is the record_user_yield{,_activity}() calls all over; these must be
	 * in every case we wish to consider to mean the user thread is *not*
	 * stuck in a yield loop. See logic in functions above for more. */
	struct sched_state *s = &ls->sched;
	unsigned int lock_addr;
	bool succeeded;
	bool write_mode;

	/* Prevent the rwlock downgrade test memorial bug. The following logic
	 * checks eip against hardcoded user addresses obtained from objdump,
	 * which may accidentally collide with totally unrelated addresses in
	 * the shell or init or idle or whatever. So we can't look for such
	 * activity if the entire address space is wrong. */
	if (!check_user_address_space(ls)) {
		return;
	}

	if (USER_MEMORY(ls->eip) && CURRENT(s, pre_vanish_trace) != NULL) {
		assert(0 && "thread went back to userspace after entering vanish");
		// FIXME(#130) uncomment below to enable more flexibility
		// free_stack_trace(CURRENT(s, pre_vanish_trace));
		// CURRENT(s, pre_vanish_trace) = NULL;
	}

	/* mutexes (and yielding) */
	if (user_mutex_init_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_mutex_initing));
		assert(!ACTION(s, user_mutex_destroying));
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_unlocking));
		assert(CURRENT(s, user_mutex_initing_addr) == -1);
		ACTION(s, user_mutex_initing) = true;
		CURRENT(s, user_mutex_initing_addr) = lock_addr;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_mutex_init_exiting(ls->eip)) {
		assert(ACTION(s, user_mutex_initing));
		assert(!ACTION(s, user_mutex_destroying));
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_unlocking));
		assert(CURRENT(s, user_mutex_initing_addr) != -1);
		CURRENT(s, user_mutex_initing_addr) = -1;
		ACTION(s, user_mutex_initing) = false;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_mutex_destroy_entering(ls->cpu0, ls->eip, &lock_addr)) {
		/* nb. lock/unlock allowed during destroy */
		assert(!ACTION(s, user_mutex_initing));
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_unlocking));
		assert(!ACTION(s, user_mutex_destroying));
		ACTION(s, user_mutex_destroying) = true;
		mutex_destroy(&ls->user_sync, lock_addr);
		record_user_yield_activity(&ls->user_sync);
	} else if (user_mutex_destroy_exiting(ls->eip)) {
		assert(!ACTION(s, user_mutex_initing));
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_unlocking));
		assert(ACTION(s, user_mutex_destroying));
		ACTION(s, user_mutex_destroying) = false;
	} else if (user_mutex_lock_entering(ls->cpu0, ls->eip, &lock_addr) ||
	           user_mutex_trylock_entering(ls->cpu0, ls->eip, &lock_addr)) {
		/* note: we don't assert on mutex_initing because mutex_init may
		 * call mutex_lock for malloc mutexes, on a static heap mutex */
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_unlocking));
		ACTION(s, user_mutex_locking) = true;
		CURRENT(s, user_mutex_locking_addr) = lock_addr;
		/* Add to lockset AROUND lock implementation, to forgive atomic
		 * ops inside of being data races. */
		if (lock_addr != 0) {
			lockset_add(s, &CURRENT(s, user_locks_held), lock_addr, LOCK_MUTEX);
		}
		record_user_mutex_activity(&ls->user_sync);
	} else if (user_yielding(ls->cpu0, ls->eip)) {
		if (ACTION(s, user_mutex_locking)) {
			/* "Probably" blocked on the mutex. */
			assert(!ACTION(s, user_mutex_unlocking));
			assert(CURRENT(s, user_mutex_locking_addr) != -1);
			assert(CURRENT(s, user_mutex_locking_addr) != 0);
			/* Could have been yielding before; gotten kicked awake but
			 * didn't get the lock, have to yield again. */
			ACTION(s, user_mutex_yielding) = true;
			CURRENT(s, user_blocked_on_addr) =
				CURRENT(s, user_mutex_locking_addr);
			record_user_mutex_activity(&ls->user_sync);
		} else {
			/* User yield outside of a mutex access. */
			record_user_yield(&ls->user_sync);
		}
	} else if (user_mutex_lock_exiting(ls->eip)) {
		unsigned int lock_addr = CURRENT(s, user_mutex_locking_addr);
		assert(ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_unlocking));
		assert(lock_addr != -1);
		ACTION(s, user_mutex_locking) = false;
		ACTION(s, user_mutex_yielding) = false;
		lsprintf(DEV, "tid %d locked mutex 0x%x\n", CURRENT(s, tid), lock_addr);
		if (lock_addr != 0) {
			user_mutex_block_others(&s->rq, lock_addr, true);
		}
		CURRENT(s, user_mutex_locking_addr) = -1;
		record_user_mutex_activity(&ls->user_sync);
	} else if (user_mutex_trylock_exiting(ls->cpu0, ls->eip, &succeeded)) {
		unsigned int lock_addr = CURRENT(s, user_mutex_locking_addr);
		assert(ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_yielding));
		assert(!ACTION(s, user_mutex_unlocking));
		assert(lock_addr != -1);
		ACTION(s, user_mutex_locking) = false;
		if (lock_addr == 0) {
			/* no-op. */
		} else if (succeeded) {
			lsprintf(DEV, "tid %d tried + could lock mutex 0x%x\n",
				 CURRENT(s, tid), lock_addr);
			user_mutex_block_others(&s->rq, lock_addr, true);
		} else {
			lsprintf(DEV, "tid %d failed to trylock mutex 0x%x\n",
				 CURRENT(s, tid), lock_addr);
			lockset_remove(s, lock_addr, LOCK_MUTEX, false);
		}
		CURRENT(s, user_mutex_locking_addr) = -1;
		record_user_mutex_activity(&ls->user_sync);
	} else if (user_mutex_unlock_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_yielding));
		assert(!ACTION(s, user_mutex_unlocking));
		ACTION(s, user_mutex_unlocking) = true;
		CURRENT(s, user_mutex_unlocking_addr) = lock_addr;
		lsprintf(DEV, "tid %d unlocks mutex 0x%x\n", CURRENT(s, tid), lock_addr);
		record_user_mutex_activity(&ls->user_sync);
	} else if (user_mutex_unlock_exiting(ls->eip)) {
		unsigned int lock_addr = CURRENT(s, user_mutex_unlocking_addr);
		assert(!ACTION(s, user_mutex_locking));
		assert(!ACTION(s, user_mutex_yielding));
		assert(ACTION(s, user_mutex_unlocking));
		assert(lock_addr != -1);
		ACTION(s, user_mutex_unlocking) = false;
		lsprintf(DEV, "tid %d unlocked mutex 0x%x\n", CURRENT(s, tid), lock_addr);
		if (lock_addr != 0) {
			lockset_remove(s, lock_addr, LOCK_MUTEX, false);
			user_mutex_block_others(&s->rq, lock_addr, false);
		}
		CURRENT(s, user_mutex_unlocking_addr) = -1;
		record_user_mutex_activity(&ls->user_sync);
	/* cvars */
	} else if (user_cond_wait_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_cond_waiting));
		ACTION(s, user_cond_waiting) = true;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_cond_wait_exiting(ls->eip)) {
		assert(ACTION(s, user_cond_waiting));
		ACTION(s, user_cond_waiting) = false;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_cond_signal_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_cond_signalling));
		ACTION(s, user_cond_signalling) = true;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_cond_signal_exiting(ls->eip)) {
		assert(ACTION(s, user_cond_signalling));
		ACTION(s, user_cond_signalling) = false;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_cond_broadcast_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_cond_broadcasting));
		ACTION(s, user_cond_broadcasting) = true;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_cond_broadcast_exiting(ls->eip)) {
		assert(ACTION(s, user_cond_broadcasting));
		ACTION(s, user_cond_broadcasting) = false;
		record_user_yield_activity(&ls->user_sync);
	/* semaphores (TODO: model logic for these) */
	} else if (user_sem_wait_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_sem_proberen));
		ACTION(s, user_sem_proberen) = true;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_sem_wait_exiting(ls->eip)) {
		assert(ACTION(s, user_sem_proberen));
		ACTION(s, user_sem_proberen) = false;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_sem_signal_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_sem_verhogen));
		ACTION(s, user_sem_verhogen) = true;
		record_user_yield_activity(&ls->user_sync);
	} else if (user_sem_signal_exiting(ls->eip)) {
		assert(ACTION(s, user_sem_verhogen));
		ACTION(s, user_sem_verhogen) = false;
		record_user_yield_activity(&ls->user_sync);
	/* rwlocks */
	} else if (user_rwlock_lock_entering(ls->cpu0, ls->eip, &lock_addr, &write_mode)) {
		assert(!ACTION(s, user_rwlock_locking));
		assert(!ACTION(s, user_rwlock_unlocking));
		ACTION(s, user_rwlock_locking) = true;
		/* use low bit of addr to store mode */
		assert((lock_addr & 0x1) == 0);
		CURRENT(s, user_rwlock_locking_addr) = lock_addr | (write_mode ? 1 : 0);
		lsprintf(DEV, "tid %d locks rwlock 0x%x for %s\n", CURRENT(s, tid),
			 lock_addr, write_mode ? "writing" : "reading");
		record_user_yield_activity(&ls->user_sync);
	} else if (user_rwlock_lock_exiting(ls->eip)) {
		assert(ACTION(s, user_rwlock_locking));
		assert(!ACTION(s, user_rwlock_unlocking));
		assert(CURRENT(s, user_rwlock_locking_addr) != -1);
		unsigned int lock_addr = CURRENT(s, user_rwlock_locking_addr) & ~0x1;
		bool write_mode = (CURRENT(s, user_rwlock_locking_addr) & 0x1) == 1;
		CURRENT(s, user_rwlock_locking_addr) = -1;
		ACTION(s, user_rwlock_locking) = false;
		lsprintf(DEV, "tid %d locked rwlock 0x%x for %s\n", CURRENT(s, tid),
			 lock_addr, write_mode ? "writing" : "reading");
		/* Add to lockset INSIDE lock implementation; i.e., we consider
		 * the implementation not protected by itself w.r.t data races. */
		lockset_add(s, &CURRENT(s, user_locks_held), lock_addr,
			    write_mode ? LOCK_RWLOCK : LOCK_RWLOCK_READ);
		record_user_yield_activity(&ls->user_sync);
	} else if (user_rwlock_unlock_entering(ls->cpu0, ls->eip, &lock_addr)) {
		assert(!ACTION(s, user_rwlock_locking));
		assert(!ACTION(s, user_rwlock_unlocking));
		ACTION(s, user_rwlock_unlocking) = true;
		lsprintf(DEV, "tid %d unlocks rwlock 0x%x\n", CURRENT(s, tid), lock_addr);
		lockset_remove(s, lock_addr, LOCK_RWLOCK, false);
		record_user_yield_activity(&ls->user_sync);
	} else if (user_rwlock_unlock_exiting(ls->eip)) {
		assert(!ACTION(s, user_rwlock_locking));
		assert(ACTION(s, user_rwlock_unlocking));
		ACTION(s, user_rwlock_unlocking) = false;
		lsprintf(DEV, "tid %d unlocked rwlock\n", CURRENT(s, tid));
		record_user_yield_activity(&ls->user_sync);
	/* thread library interface */
	} else if (user_thr_init_entering(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	} else if (user_thr_init_exiting(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	} else if (user_thr_create_entering(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	} else if (user_thr_create_exiting(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	} else if (user_thr_join_entering(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	} else if (user_thr_join_exiting(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	} else if (user_thr_exit_entering(ls->eip)) {
		record_user_yield_activity(&ls->user_sync);
	/* locking malloc wrappers */
	} else if (user_locked_malloc_entering(ls->eip)) {
		assert(!ACTION(s, user_locked_mallocing));
		ACTION(s, user_locked_mallocing) = true;
	} else if (user_locked_malloc_exiting(ls->eip)) {
		assert(ACTION(s, user_locked_mallocing));
		ACTION(s, user_locked_mallocing) = false;
	} else if (user_locked_free_entering(ls->eip)) {
		assert(!ACTION(s, user_locked_freeing));
		ACTION(s, user_locked_freeing) = true;
	} else if (user_locked_free_exiting(ls->eip)) {
		assert(ACTION(s, user_locked_freeing));
		ACTION(s, user_locked_freeing) = false;
	} else if (user_locked_calloc_entering(ls->eip)) {
		assert(!ACTION(s, user_locked_callocing));
		ACTION(s, user_locked_callocing) = true;
	} else if (user_locked_calloc_exiting(ls->eip)) {
		assert(ACTION(s, user_locked_callocing));
		ACTION(s, user_locked_callocing) = false;
	} else if (user_locked_realloc_entering(ls->eip)) {
		assert(!ACTION(s, user_locked_reallocing));
		ACTION(s, user_locked_reallocing) = true;
	} else if (user_locked_realloc_exiting(ls->eip)) {
		assert(ACTION(s, user_locked_reallocing));
		ACTION(s, user_locked_reallocing) = false;
	/* misc */
	} else if (user_make_runnable_entering(ls->eip)) {
		/* Catch "while (make_runnable(tid) < 0)" loops. */
		record_user_yield(&ls->user_sync);
	} else if (user_sleep_entering(ls->eip)) {
		/* Catch "while (!ready) sleep()" loops. */
		record_user_yield(&ls->user_sync);
	}
}

void sched_update(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	unsigned int old_tid = CURRENT(s, tid);
	unsigned int new_tid;

	/* wait until the guest is ready */
	if (!s->guest_init_done) {
		if (kern_sched_init_done(ls->eip)) {
			s->guest_init_done = true;
			/* Deprecated since kern_get_current_tid went away. */
			// assert(old_tid == new_tid && "init tid mismatch");
		} else {
			sched_check_lmm_init(ls);
			sched_check_pintos_init_sequence(ls);
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
		if (kern_timer_entering(ls->eip)) {
			lsprintf(DEV, "Suppressing unwanted timer tick from simics "
				 "(received at 0x%x).\n", READ_STACK(ls->cpu0, 0));
			ls->eip = avoid_timer_interrupt_immediately(ls->cpu0);
			// Print whether it thinks anybody's alive.
			anybody_alive(ls->cpu0, &ls->test, s, true);
			// Dump scheduler state, too.
			lsprintf(DEV, "scheduler state: ");
			print_qs(DEV, s);
			printf(DEV, "\n");
		}
	}

	/**********************************************************************
	 * Update scheduler state.
	 **********************************************************************/

	if (kern_thread_switch(ls->cpu0, ls->eip, &new_tid) && new_tid != old_tid) {
		/*
		 * So, fork needs to be handled twice, both here and below in the
		 * runnable case. And for kernels that trigger both, both places will
		 * need to have a check for whether the newly forked thread exists
		 * already.
		 *
		 * Sleep and vanish actually only need to happen here. They should
		 * check both the rq and the dq, 'cause there's no telling where the
		 * thread got moved to before. As for the descheduling case, that needs
		 * to check for a new type of action flag "asleep" or "vanished" (and
		 * I guess using last_vanished_agent might work), and probably just
		 * assert that that condition holds if the thread couldn't be found
		 * for the normal descheduling case.
		 */
		/* Has to be handled before updating cur_agent, of course. */
		handle_sleep(s);
		handle_vanish(s);
		handle_unsleep(s, new_tid);

		/* Careful! On some kernels, the trigger for a new agent forking
		 * (where it first gets added to the RQ) may happen AFTER its
		 * tcb is set to be the currently running thread. This would
		 * cause this case to be reached before agent_fork() is called,
		 * so agent_by_tid would fail. Instead, we have an option to
		 * find it later. (see the kern_thread_runnable case below.) */
		struct agent *next = agent_by_tid_or_null(&s->rq, new_tid);
		if (next == NULL) next = agent_by_tid_or_null(&s->dq, new_tid);

		if (next != NULL) {
			lsprintf(DEV, "switched threads %d -> %d\n", old_tid,
				 new_tid);
			s->last_agent = s->cur_agent;
			s->cur_agent = next;
		/* This fork check is for kernels which context switch to a
		 * newly-forked thread before adding it to the runqueue - and
		 * possibly won't do so at all (if current_extra_runnable). We
		 * need to do agent_fork now. (agent_fork *also* needs to be
		 * handled below, for kernels which don't c-s to the new thread
		 * immediately.) The */
		} else if (handle_fork(s, new_tid, false)) {
			next = agent_by_tid_or_null(&s->dq, new_tid);
			assert(next != NULL && "Newly forked thread not on DQ");
			lsprintf(DEV, "switching threads %d -> %d\n", old_tid,
				 new_tid);
			s->last_agent = s->cur_agent;
			s->cur_agent = next;
		} else {
			lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Couldn't find "
				 "new thread %d; current %d; did you forget to "
				 "tell_landslide_forking()?\n" COLOUR_DEFAULT,
				 new_tid, CURRENT(s, tid));
			assert(0);
		}
		/* Some debug info to help the studence. */
		if (TID_IS_INIT(CURRENT(s, tid))) {
			lskprintf(DEV, "Now running init.\n");
		} else if (TID_IS_SHELL(CURRENT(s, tid))) {
			lskprintf(DEV, "Now running shell.\n");
		} else if (TID_IS_IDLE(CURRENT(s, tid))) {
			lskprintf(DEV, "Now idling.\n");
		}
	}

	s->current_extra_runnable = kern_current_extra_runnable(ls->cpu0);

	/* Skip state machine update if we expect this is a "duplicate"
	 * instruction caused by delaying for a data race PP. See #144. */
	// XXX: There is a potential problem (of unknown severity) here. Note
	// XXX: that we are skipping the state machine update for the *SECOND*
	// XXX: time we get called for the delayed instruction. But that time
	// XXX: is when it really gets executed. So we may end up prematurely
	// XXX: updating the state machine to reflect changes (e.g. in mutex
	// XXX: ownership) that might not have happened before a context
	// XXX: switch to another thread.
	if (!(CURRENT(s, just_delayed_for_data_race) &&
	      CURRENT(s, delayed_data_race_eip == ls->eip))) {
		if (KERNEL_MEMORY(ls->eip)) {
			sched_update_kern_state_machine(ls);
		} else {
			sched_update_user_state_machine(ls);
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
			 * why we leave the schedule_target flag turned on).
			 * the special case is for newly forked agents that are
			 * schedule targets - they won't exit timer or c-s above
			 * so here is where we have to clear it for them. */
			if (ACTION(s, just_forked)) {
				/* Interrupts are "probably" off, but that's why
				 * just_finished_reschedule is persistent. */
				lskprintf(DEV, "Finished flying to %d.\n",
				          CURRENT(s, tid));
				ACTION(s, just_forked) = false;
				s->just_finished_reschedule = true;
				sched_finish_inflight(s);
			} else {
				assert(ACTION(s, cs_free_pass) ||
				       ACTION(s, context_switch) ||
				       HANDLING_INTERRUPT(s));
			}
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
			      kern_context_switch_exiting(ls->eip)) ||
			     ACTION(s, just_forked)) {
				/* an undesirable agent just got switched to;
				 * keep the pending schedule in the air. */
				// XXX: this seems to get taken too soon? change
				// it somehow to cause_.._immediately. and then
				// see the asserts/comments in the action
				// handling_timer sections above.
				/* some kernels (pathos) still have interrupts
				 * off or scheduler locked at this point; so
				 * properties of !R */
				if (interrupts_enabled(ls->cpu0) &&
				    kern_ready_for_timer_interrupt(ls->cpu0)) {
					lskprintf(INFO, "keeping schedule in-"
					          "flight at 0x%x\n", ls->eip);
					keep_schedule_inflight(ls);
				} else {
					lskprintf(INFO, "Want to keep schedule "
					          "in-flight at 0x%x; have to "
					          "delay\n", ls->eip);
					s->delayed_in_flight = true;
				}
				/* If this was the special case where the
				 * undesirable thread was just forked, keeping
				 * the schedule in flight will cause it to do a
				 * normal context switch. So just_forked is no
				 * longer needed. */
				ACTION(s, just_forked) = false;
			} else if (s->delayed_in_flight &&
				   interrupts_enabled(ls->cpu0) &&
				   kern_ready_for_timer_interrupt(ls->cpu0)) {
				lskprintf(INFO, "Delayed in-flight timer tick "
				          "at 0x%x\n", ls->eip);
				keep_schedule_inflight(ls);
			} else {
				/* they'd better not have "escaped" */
				assert(ACTION(s, cs_free_pass) ||
				       ACTION(s, context_switch) ||
				       HANDLING_INTERRUPT(s) ||
				       !interrupts_enabled(ls->cpu0) ||
				       !kern_ready_for_timer_interrupt(ls->cpu0));
			}
		}
		/* in any case we have no more decisions to make here */
		return;
	} else if (ACTION(s, just_forked)) {
		ACTION(s, just_forked) = false;
		s->just_finished_reschedule = true;
	}
	assert(!s->schedule_in_flight);

	/* Can't do anything before the test actually starts. */
	if (ls->test.current_test == NULL) {
		return;
	}

	/* XXX: This will "leak" an undesirable thread to execute an
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

	/* In the far future we may want an extra mode which will allow us to
	 * preempt the timer handler. */
	if (HANDLING_INTERRUPT(s) || !kern_ready_for_timer_interrupt(ls->cpu0)) {
		return;
	}

	/* As kernel_specifics.h says, no preempting during mutex unblocking. */
	if (ACTION(s, kern_mutex_unlocking)) {
		return;
	}

	/* Okay, are we at a choice point? */
	bool voluntary, need_handle_sleep, data_race;
	bool just_finished_reschedule = s->just_finished_reschedule;
	s->just_finished_reschedule = false;
	if (arbiter_interested(ls, just_finished_reschedule, &voluntary,
			       &need_handle_sleep, &data_race)) {
		struct agent *current = voluntary ? s->last_agent : s->cur_agent;
		struct agent *chosen;
		bool our_choice;

		assert(!(data_race && voluntary));

		/* Avoid infinite stuckness if we just inserted a DR PP. */
		if (data_race && CURRENT(s, just_delayed_for_data_race)) {
			lsprintf(ALWAYS, "just delayed @ %x\n", ls->eip);
			assert(CURRENT(s, delayed_data_race_eip) != -1);
			if (ls->eip == CURRENT(s, delayed_data_race_eip)) {
				lsprintf(ALWAYS, "just delayed ends\n");
				/* Delayed data race instruction was reached.
				 * Allow arbiter to insert new PPs again. */
				CURRENT(s, just_delayed_for_data_race) = false;
				CURRENT(s, delayed_data_race_eip) = -1;
			}
			return;
		}

		/* Some kernels that don't have separate idle threads (POBBLES)
		 * may sleep() a thread and go into hlt state without ever
		 * switching the current thread (where we would handle_sleep()
		 * above). So need to handle it again here. */
		if (need_handle_sleep) {
			handle_sleep(s);
		}

		/* Right before the decision point is recorded, update the user
		 * yield loop counter so the yield/activity tracker is clean.
		 * Must also happen before the arbiter's choice (in case this
		 * check causes us to consider the thread blocked, the arbiter
		 * must not choose it). */
		check_user_yield_activity(&ls->user_sync, current);

		if (arbiter_choose(ls, current, &chosen, &our_choice)) {
			int data_race_eip = -1;
			if (data_race) {
				/* Is this a "fake" preemption point? If so we
				 * are not to forcibly preempt, only to record
				 * a save point. */
				lsprintf(DEV, "data race PP; overriding arbiter"
					 " choice %d with current %d\n",
					 chosen->tid, current->tid);
				dump_stack();
				chosen = current;
				data_race_eip = ls->eip;
				/* Insert a dummy instruction before creating
				 * the save point, so the racing instruction
				 * occurs in the transition *after* that PP. */
				CURRENT(s, just_delayed_for_data_race) = true;
				CURRENT(s, delayed_data_race_eip) = ls->eip;
				ls->eip = delay_instruction(ls->cpu0);
			}
			/* Effect the choice that was made... */
			if (chosen != s->cur_agent ||
			    agent_by_tid_or_null(&s->sq, CURRENT(s, tid)) != NULL) {
				if (chosen == s->cur_agent) {
					/* The arbiter picked to wake a thread
					 * off the sleep queue. Prevent it from
					 * getting confused about who "last" ran
					 * when the next choice point happens. */
					s->last_agent = s->cur_agent;
				}
				lsprintf(DEV, "from agent %d, arbiter chose "
					 "%d at 0x%x (called at 0x%x)\n",
					 CURRENT(s, tid), chosen->tid, ls->eip,
					 (unsigned int)READ_STACK(ls->cpu0, 0));
				set_schedule_target(s, chosen);
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
			}
			/* Record the choice that was just made. */
			if (ls->test.test_ever_caused &&
			    ls->test.start_population != s->most_agents_ever) {
				save_setjmp(&ls->save, ls, chosen->tid,
					    our_choice, false, !data_race,
					    data_race_eip, voluntary);
			}
		} else {
			lsprintf(DEV, "no agent was chosen at eip 0x%x\n",
				 ls->eip);
			lsprintf(DEV, "scheduler state: ");
			print_qs(DEV, s);
			printf(DEV, "\n");
		}
	} else if (CURRENT(s, just_delayed_for_data_race)) {
		/* ensure arbiter is consistently interested in data race
		 * locations (otherwise just-delayed won't be properly unset) */
		assert(ls->eip != CURRENT(s, delayed_data_race_eip));
	}
	/* XXX TODO: it may be that not every timer interrupt triggers a context
	 * switch, so we should watch out if a handler doesn't enter the c-s. */
}

void sched_recover(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	unsigned int tid;

	assert(ls->just_jumped);

	if (arbiter_pop_choice(&ls->arbiter, &tid)) {
		if (tid == CURRENT(s, tid)) {
			/* Hmmmm */
			if (kern_timer_entering(ls->eip)) {
				/* Oops, we ended up trying to leave the thread
				 * we want to be running. Make sure to go
				 * back... */
				set_schedule_target(s, s->cur_agent);
				assert(s->entering_timer);
				lsprintf(INFO, "Explorer-chosen tid %d wants "
					 "to run; not switching away\n", tid);
				/* Make sure the arbiter knows this isn't a
				 * voluntary reschedule. The handling_timer flag
				 * won't be on now, but sched_update sets it. */
				lsprintf(INFO, "Updating the last_agent: ");
				print_agent(INFO, s->last_agent);
				printf(INFO, " to ");
				print_agent(INFO, s->cur_agent);
				printf(INFO, "\n");
				s->last_agent = s->cur_agent;
				/* This will cause an assert to trip faster. */
				s->voluntary_resched_tid = -1;
			} else {
				lsprintf(INFO, "Explorer-chosen tid %d already "
					 "running!\n", tid);
			}
		} else {
			// TODO: duplicate agent search logic (arbiter.c)
			struct agent *a = agent_by_tid_or_null(&s->rq, tid);
			if (a == NULL) {
				a = agent_by_tid_or_null(&s->sq, tid);
			}

			assert(a != NULL && "bogus explorer-chosen tid!");
			lsprintf(INFO, "Recovering to explorer-chosen tid %d "
				 "from tid %d\n", tid, CURRENT(s, tid));
			set_schedule_target(s, a);
			/* Hmmmm */
			if (!kern_timer_entering(ls->eip)) {
				ls->eip = cause_timer_interrupt_immediately(
					ls->cpu0);
			}
			s->entering_timer = true;
		}
	} else {
		tid = CURRENT(s, tid);
		lsprintf(BUG, "Explorer chose no tid; defaulting to %d\n", tid);
	}

	save_recover(&ls->save, ls, tid);
}
