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

static struct agent *agent_by_tid(struct agent_q *q, int tid)
{
	struct agent *a = agent_by_tid_or_null(q, tid);
	if (a == NULL) {
		conf_object_t *cpu = SIM_get_object("cpu0");
		char *stack = stack_trace(cpu, GET_CPU_ATTR(cpu, eip), -1);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "TID %d isn't in the "
			 "right queue; probably incorrect annotations?\n", tid);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Current stack: %s\n"
			 COLOUR_DEFAULT, stack);
		assert(0);
	}
	return a;
}

/* Call with whether or not the thread is created with a context-switch frame
 * crafted on its stack. Most threads would be; "init" may not be. */
static void agent_fork(struct sched_state *s, int tid, bool on_runqueue)
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
	a->action.mutex_locking = false;
	a->action.mutex_unlocking = false;
	a->action.schedule_target = false;
	a->blocked_on = NULL;
	a->blocked_on_tid = -1;
	a->blocked_on_addr = -1;

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
	struct agent *a = agent_by_tid_or_null(&s->rq, tid);
	if (a != NULL) {
		Q_REMOVE(&s->rq, a, nobe);
		Q_INSERT_FRONT(&s->dq, a, nobe);
	/* If it's not on the runqueue, we must have already special-case moved
	 * it off in the thread-change event. */
	} else if (agent_by_tid_or_null(&s->sq, tid) == NULL) {
		/* Either it's on the sleep queue, or it vanished. */
		if (agent_by_tid_or_null(&s->dq, tid) != NULL) {
			conf_object_t *cpu = SIM_get_object("cpu0");
			char *stack = stack_trace(cpu, GET_CPU_ATTR(cpu, eip),
						  s->cur_agent->tid);
			lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "TID %d is "
				 "already off the runqueue at tell_off_rq(); "
				 "probably incorrect annotations?\n", tid);
			lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Current stack: %s\n"
				 COLOUR_DEFAULT, stack);
			assert(0);
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
		MM_FREE(s->last_vanished_agent);
	}
	s->last_vanished_agent = s->cur_agent;
	s->num_agents--;
}

static void set_schedule_target(struct sched_state *s, struct agent *a)
{
	if (s->schedule_in_flight != NULL) {
		lsprintf(DEV, "warning: overriding old schedule target ");
		print_agent(DEV, s->schedule_in_flight);
		printf(DEV, " with new ");
		print_agent(DEV, a);
		printf(DEV, "\n");
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

static void print_deadlock(verbosity v, struct agent *a)
{
	struct agent *start = a;
	printf(v, "(%d", a->tid);
	for (a = a->blocked_on; a != start; a = a->blocked_on) {
		assert(a != NULL && "a wasn't deadlocked!");
		printf(v, " -> %d", a->tid);
	}
	printf(v, " -> %d)", a->tid);
}

static void mutex_block_others(struct agent_q *q, int mutex_addr,
			       struct agent *blocked_on, int blocked_on_tid)
{
	struct agent *a;
	Q_FOREACH(a, q, nobe) {
		if (a->blocked_on_addr == mutex_addr) {
			lsprintf(DEV, "mutex: on 0x%x tid %d now blocks on %d "
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
	s->guest_init_done = false; /* must be before kern_init_threads */
	kern_init_threads(s, agent_fork);
	s->cur_agent = agent_by_tid_or_null(&s->rq, kern_get_first_tid());
	if (s->cur_agent == NULL)
		s->cur_agent = agent_by_tid(&s->dq, kern_get_first_tid());
	s->last_agent = NULL;
	s->last_vanished_agent = NULL;
	s->schedule_in_flight = NULL;
	s->delayed_in_flight = false;
	s->just_finished_reschedule = false;
	s->entering_timer = false;
	s->voluntary_resched_tid = -1;
	s->voluntary_resched_stack = NULL;
}

void print_agent(verbosity v, struct agent *a)
{
	printf(v, "%d", a->tid);
	if (a->action.handling_timer)  printf(v, "t");
	if (a->action.context_switch)  printf(v, "c");
	if (a->action.forking)         printf(v, "f");
	if (a->action.sleeping)        printf(v, "s");
	if (a->action.vanishing)       printf(v, "v");
	if (a->action.readlining)      printf(v, "r");
	if (a->action.schedule_target) printf(v, "*");
	if (BLOCKED(a))                printf(v, "<?%d>", a->blocked_on_tid);
}

void print_q(verbosity v, const char *start, struct agent_q *q, const char *end)
{
	struct agent *a;
	bool first = true;

	printf(v, "%s", start);
	Q_FOREACH(a, q, nobe) {
		if (first)
			first = false;
		else
			printf(v, ", ");
		print_agent(v, a);
	}
	printf(v, "%s", end);
}
void print_qs(verbosity v, struct sched_state *s)
{
	printf(v, "current ");
	print_agent(v, s->cur_agent);
	printf(v, " ");
	print_q(v, " RQ [", &s->rq, "] ");
	print_q(v, " SQ {", &s->sq, "} ");
	print_q(v, " DQ (", &s->dq, ") ");
}

/* what is the current thread doing? */
#define ACTION(s, act) ((s)->cur_agent->action.act)
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
		conf_object_t *cpu = SIM_get_object("cpu0");
		char *stack = stack_trace(cpu, GET_CPU_ATTR(cpu, eip),
					  s->cur_agent->tid);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "While trying to do %s;"
			 " probably incorrect annotations?\n", new_act);
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Current stack: %s\n",
			 stack);
		assert(0);
	}
}
#undef CHECK_NOT_ACTION

static bool handle_fork(struct sched_state *s, int target_tid, bool add_to_rq)
{
	if (ACTION(s, forking) && !HANDLING_INTERRUPT(s) &&
	    s->cur_agent->tid != target_tid) {
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
		lsprintf(DEV, "agent %d sleep -- ", s->cur_agent->tid);
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
		lsprintf(DEV, "agent %d vanish -- ", s->cur_agent->tid);
		print_qs(DEV, s);
		printf(DEV, "\n");
		agent_vanish(s);
		/* the vanishing flag stays on (TODO: is it needed?) */
	}
}
static void handle_unsleep(struct sched_state *s, int tid)
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

void sched_update(struct ls_state *ls)
{
	struct sched_state *s = &ls->sched;
	int old_tid = s->cur_agent->tid;
	int new_tid;

	/* wait until the guest is ready */
	if (!s->guest_init_done) {
		if (kern_sched_init_done(ls->eip)) {
			s->guest_init_done = true;
			/* Deprecated since kern_get_current_tid went away. */
			// assert(old_tid == new_tid && "init tid mismatch");
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
		if (kern_timer_entering(ls->eip)) {
			lsprintf(DEV, "A timer tick that wasn't ours (0x%x).\n",
				 (int)READ_STACK(ls->cpu0, 0));
			ls->eip = avoid_timer_interrupt_immediately(ls->cpu0);
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
				 new_tid, s->cur_agent->tid);
			assert(0);
		}
		/* Some debug info to help the studence. */
		if (s->cur_agent->tid == kern_get_init_tid()) {
			lsprintf(DEV, "Now running init.\n");
		} else if (s->cur_agent->tid == kern_get_shell_tid()) {
			lsprintf(DEV, "Now running shell.\n");
		} else if (kern_has_idle() &&
			   s->cur_agent->tid == kern_get_idle_tid()) {
			lsprintf(DEV, "Now idling.\n");
		}
	}

	s->current_extra_runnable = kern_current_extra_runnable(ls->cpu0);

	int target_tid;
	int mutex_addr;

	/* Timer interrupt handling. */
	if (kern_timer_entering(ls->eip)) {
		// XXX: same as the comment in the below condition.
		if (!kern_timer_exiting(READ_STACK(ls->cpu0, 0))) {
			assert(!ACTION(s, handling_timer));
		} else {
			lsprintf(DEV, "WARNING: allowing a nested timer on "
				 "tid %d's stack\n", s->cur_agent->tid);
		}
		ACTION(s, handling_timer) = true;
		lsprintf(INFO, "%d timer enter from 0x%x\n", s->cur_agent->tid,
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
				ACTION(s, schedule_target) = false;
				s->schedule_in_flight = NULL;
			}
		} else {
			lsprintf(INFO, "WARNING: exiting a non-timer interrupt "
				 "through a path shared with the timer..? (from 0x%x, #%d)\n", (int)READ_STACK(ls->cpu0, 0), (int)READ_STACK(ls->cpu0, -2));
		}
	/* Context switching. */
	} else if (kern_context_switch_entering(ls->eip)) {
		/* It -is- possible for a context switch to interrupt a
		 * context switch if a timer goes off before c-s disables
		 * interrupts. TODO: if we care, make this an int counter. */
		ACTION(s, context_switch) = true;
		/* Maybe update the voluntary resched trace. See schedule.h */
		if (!ACTION(s, handling_timer)) {
			lsprintf(DEV, "Voluntary resched tid ");
			print_agent(DEV, s->cur_agent);
			printf(DEV, "\n");
			s->voluntary_resched_tid = s->cur_agent->tid;
			if (s->voluntary_resched_stack != NULL)
				MM_FREE(s->voluntary_resched_stack);
			s->voluntary_resched_stack =
				stack_trace(ls->cpu0, ls->eip, s->cur_agent->tid);
		}
	} else if (kern_context_switch_exiting(ls->eip)) {
		assert(ACTION(s, cs_free_pass) || ACTION(s, context_switch));
		ACTION(s, context_switch) = false;
		ACTION(s, cs_free_pass) = false;
		/* For threads that context switched of their own accord. */
		if (!HANDLING_INTERRUPT(s)) {
			s->just_finished_reschedule = true;
			if (ACTION(s, schedule_target)) {
				ACTION(s, schedule_target) = false;
				s->schedule_in_flight = NULL;
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
	} else if (kern_mutex_locking(ls->cpu0, ls->eip, &mutex_addr)) {
		//assert(!ACTION(s, mutex_locking));
		assert(!ACTION(s, mutex_unlocking));
		ACTION(s, mutex_locking) = true;
		s->cur_agent->blocked_on_addr = mutex_addr;
	} else if (kern_mutex_blocking(ls->cpu0, ls->eip, &target_tid)) {
		/* Possibly not the case - if this thread entered mutex_lock,
		 * then switched and someone took it, these would be set already
		 * assert(s->cur_agent->blocked_on == NULL);
		 * assert(s->cur_agent->blocked_on_tid == -1); */
		lsprintf(DEV, "mutex: on 0x%x tid %d blocks, owned by %d\n",
			 s->cur_agent->blocked_on_addr, s->cur_agent->tid,
			 target_tid);
		s->cur_agent->blocked_on_tid = target_tid;
		if (deadlocked(s)) {
			lsprintf(BUG, COLOUR_BOLD COLOUR_RED "DEADLOCK! ");
			print_deadlock(BUG, s->cur_agent);
			printf(BUG, "\n");
			found_a_bug(ls);
		}
	} else if (kern_mutex_locking_done(ls->eip)) {
		//assert(ACTION(s, mutex_locking));
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
		 * (and it can happen), or mutex_lock inside of mutex_lock, but
		 * not the other way around. */
		assert(!ACTION(s, mutex_unlocking));
		ACTION(s, mutex_unlocking) = true;
		mutex_block_others(&s->rq, mutex_addr, NULL, -1);
	} else if (kern_mutex_unlocking_done(ls->eip)) {
		assert(ACTION(s, mutex_unlocking));
		ACTION(s, mutex_unlocking) = false;
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
				lsprintf(DEV, "Finished flying to %d.\n",
					 s->cur_agent->tid);
				ACTION(s, schedule_target) = false;
				ACTION(s, just_forked) = false;
				s->schedule_in_flight = NULL;
				s->just_finished_reschedule = true;
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
					lsprintf(INFO, "keeping schedule in-"
						 "flight at 0x%x\n", ls->eip);
					cause_timer_interrupt(ls->cpu0);
					s->entering_timer = true;
					s->delayed_in_flight = false;
				} else {
					lsprintf(INFO, "Want to keep schedule "
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
				lsprintf(INFO, "Delayed in-flight timer tick "
					 "at 0x%x\n", ls->eip);
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
				s->delayed_in_flight = false;
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
	if (HANDLING_INTERRUPT(s) || !kern_ready_for_timer_interrupt(ls->cpu0)) {
		return;
	}

	/* As kernel_specifics.h says, no preempting during mutex unblocking. */
	if (ACTION(s, mutex_unlocking)) {
		return;
	}

	/* Okay, are we at a choice point? */
	bool voluntary;
	bool just_finished_reschedule = s->just_finished_reschedule;
	s->just_finished_reschedule = false;
	/* TODO: arbiter may also want to see the trace_entry_t */
	if (arbiter_interested(ls, just_finished_reschedule, &voluntary)) {
		struct agent *a;
		bool our_choice;
		/* TODO: as an optimisation (in serialisation state / etc), the
		 * arbiter may return NULL if there was only one possible
		 * choice. */
		if (arbiter_choose(ls, &a, &our_choice)) {
			/* Effect the choice that was made... */
			if (a != s->cur_agent) {
				lsprintf(CHOICE, "from agent %d, arbiter chose "
					 "%d at 0x%x (called at 0x%x)\n",
					 s->cur_agent->tid, a->tid, ls->eip,
					 (unsigned int)READ_STACK(ls->cpu0, 0));
				set_schedule_target(s, a);
				cause_timer_interrupt(ls->cpu0);
				s->entering_timer = true;
			}
			/* Record the choice that was just made. */
			if (ls->test.test_ever_caused &&
			    ls->test.start_population != s->most_agents_ever) {
				save_setjmp(&ls->save, ls, a->tid, our_choice,
					    false, voluntary);
			}
		} else {
			lsprintf(BUG, "no agent was chosen at eip 0x%x\n",
				 ls->eip);
			lsprintf(BUG, "scheduler state: ");
			print_qs(BUG, s);
			printf(BUG, "\n");
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
				lsprintf(DEV, "Explorer-chosen tid %d wants "
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
			lsprintf(DEV, "Recovering to explorer-chosen tid %d from "
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
		lsprintf(BUG, "Explorer chose no tid; defaulting to %d\n", tid);
	}

	save_recover(&ls->save, ls, tid);
}
