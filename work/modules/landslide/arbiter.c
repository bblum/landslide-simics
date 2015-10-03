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
#include "found_a_bug.h"
#include "kernel_specifics.h"
#include "kspec.h"
#include "landslide.h"
#include "memory.h"
#include "rand.h"
#include "schedule.h"
#include "user_specifics.h"
#include "user_sync.h"
#include "x86.h"

void arbiter_init(struct arbiter_state *r)
{
	Q_INIT_HEAD(&r->choices);
}

// FIXME: do these need to be threadsafe?
void arbiter_append_choice(struct arbiter_state *r, unsigned int tid)
{
	struct choice *c = MM_XMALLOC(1, struct choice);
	c->tid = tid;
	Q_INSERT_FRONT(&r->choices, c, nobe);
}

bool arbiter_pop_choice(struct arbiter_state *r, unsigned int *tid)
{
	struct choice *c = Q_GET_TAIL(&r->choices);
	if (c) {
		lsprintf(DEV, "using requested tid %d\n", c->tid);
		Q_REMOVE(&r->choices, c, nobe);
		*tid = c->tid;
		MM_FREE(c);
		return true;
	} else {
		return false;
	}
}

// TODO: move this to a data_race.c when that factor is done
static bool suspected_data_race(struct ls_state *ls)
{
	/* [i][0] is instruction pointer of the data race;
	 * [i][1] is the current TID when the race was observed;
	 * [i][2] is the last_call'ing eip value, if any;
	 * [i][3] is the most_recent_syscall when the race was observed. */
	static const unsigned int data_race_info[][4] = DATA_RACE_INFO;

#ifndef PINTOS_KERNEL
	// FIXME: Make this work for Pebbles kernel-space testing too.
	// Make the condition more precise (include testing_userspace() at least).
	if (!check_user_address_space(ls)) {
		return false;
	}
#endif

	for (int i = 0; i < ARRAY_SIZE(data_race_info); i++) {
		if (KERNEL_MEMORY(data_race_info[i][0])) {
#ifndef PINTOS_KERNEL
			assert(data_race_info[i][3] != 0);
#endif
		} else {
			assert(data_race_info[i][3] == 0);
		}

		if (data_race_info[i][0] == ls->eip &&
		    (data_race_info[i][1] == DR_TID_WILDCARD ||
		     data_race_info[i][1] == ls->sched.cur_agent->tid) &&
		    (data_race_info[i][2] == 0 || /* last_call=0 -> anything */
		     data_race_info[i][2] == ls->sched.cur_agent->last_call) &&
		    data_race_info[i][3] == ls->sched.cur_agent->most_recent_syscall) {
			return true;
		}
	}
	return false;
}

#define ASSERT_ONE_THREAD_PER_PP(ls) do {					\
		assert((/* root pp not created yet */				\
		        (ls)->save.next_tid == -1 ||				\
		        /* thread that was chosen is still running */		\
		        (ls)->save.next_tid == (ls)->sched.cur_agent->tid) &&	\
		       "One thread per preemption point invariant violated!");	\
	} while (0)

bool arbiter_interested(struct ls_state *ls, bool just_finished_reschedule,
			bool *voluntary, bool *need_handle_sleep, bool *data_race)
{
	*voluntary = false;
	*need_handle_sleep = false;
	*data_race = false;

	// TODO: more interesting choice points

	/* Attempt to see if a "voluntary" reschedule is just ending - did the
	 * last thread context switch not because of a timer?
	 * Also make sure to ignore null switches (timer-driven or not). */
	if (ls->sched.last_agent != NULL &&
	    !ls->sched.last_agent->action.handling_timer &&
	    ls->sched.last_agent != ls->sched.cur_agent &&
	    just_finished_reschedule) {
		lsprintf(DEV, "a voluntary reschedule: ");
		print_agent(DEV, ls->sched.last_agent);
		printf(DEV, " to ");
		print_agent(DEV, ls->sched.cur_agent);
		printf(DEV, "\n");
#ifndef PINTOS_KERNEL
		/* Pintos includes a semaphore implementation which can go
		 * around its anti-paradise-lost while loop a full time without
		 * interrupts coming back on. So, there can be a voluntary
		 * reschedule sequence where an uninterruptible, blocked thread
		 * gets jammed in the middle of this transition. Issue #165. */
		if (ls->save.next_tid != ls->sched.last_agent->tid) {
			ASSERT_ONE_THREAD_PER_PP(ls);
		}
#endif
		assert(ls->sched.voluntary_resched_tid != -1);
		*voluntary = true;
		return true;
	/* is the kernel idling, e.g. waiting for keyboard input? */
	} else if (ls->instruction_text[0] == OPCODE_HLT) {
		lskprintf(INFO, "What are you waiting for? (HLT state)\n");
		*need_handle_sleep = true;
		ASSERT_ONE_THREAD_PER_PP(ls);
		return true;
	/* Skip the instructions before the test case itself gets started. In
	 * many kernels' cases this will be redundant, but just in case. */
	} else if (!ls->test.test_ever_caused ||
		   ls->test.start_population == ls->sched.most_agents_ever) {
		return false;
	/* check for data races */
	} else if (suspected_data_race(ls)
		   /* if xchg-blocked, need NOT set DR PP. other case below. */
		   && !XCHG_BLOCKED(&ls->sched.cur_agent->user_yield)
#ifdef DR_PPS_RESPECT_WITHIN_FUNCTIONS
		   && ((testing_userspace() && user_within_functions(ls)) ||
		      (!testing_userspace() && kern_within_functions(ls)))
#endif
		   ) {
		*data_race = true;
		ASSERT_ONE_THREAD_PER_PP(ls);
		return true;
	/* user-mode-only preemption points */
	} else if (testing_userspace()) {
		unsigned int mutex_addr;
		if (KERNEL_MEMORY(ls->eip)) {
#ifdef GUEST_YIELD_ENTER
#ifndef GUEST_YIELD_EXIT
			STATIC_ASSERT(false && "missing guest yield exit");
#endif
			if ((ls->eip == GUEST_YIELD_ENTER &&
			     READ_STACK(ls->cpu0, 1) == ls->sched.cur_agent->tid) ||
			    (ls->eip == GUEST_YIELD_EXIT &&
			     ((signed int)GET_CPU_ATTR(ls->cpu0, eax)) < 0)) {
				/* Busted yield. Pretend it was yield -1. */
				ASSERT_ONE_THREAD_PER_PP(ls);
				return true;
			}
#endif
			return false;
		} else if (XCHG_BLOCKED(&ls->sched.cur_agent->user_yield)) {
			/* User thread is blocked on an "xchg-continue" mutex.
			 * Analogous to HLT state -- need to preempt it. */
			ASSERT_ONE_THREAD_PER_PP(ls);
			return true;
		} else if ((user_mutex_lock_entering(ls->cpu0, ls->eip, &mutex_addr) ||
			    user_mutex_unlock_exiting(ls->eip)) &&
			   user_within_functions(ls)) {
			ASSERT_ONE_THREAD_PER_PP(ls);
			return true;
		} else {
			return false;
		}
	/* kernel-mode-only preemption points */
#ifdef PINTOS_KERNEL
	} else if ((ls->eip == GUEST_SEMA_DOWN_ENTER || ls->eip == GUEST_SEMA_UP_EXIT) && kern_within_functions(ls)) {
		ASSERT_ONE_THREAD_PER_PP(ls);
		return true;
#endif
	} else if (kern_decision_point(ls->eip) &&
		   kern_within_functions(ls)) {
		ASSERT_ONE_THREAD_PER_PP(ls);
		return true;
	} else {
		return false;
	}
}

static bool report_deadlock(struct ls_state *ls)
{
	if (BUG_ON_THREADS_WEDGED == 0) {
		return false;
	}

	if (!anybody_alive(ls->cpu0, &ls->test, &ls->sched, true)) {
		/* No threads exist. Not a deadlock, but rather end of test. */
		return false;
	}

	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		if (BLOCKED(a) && a->action.disk_io) {
			lsprintf(CHOICE, COLOUR_BOLD COLOUR_YELLOW "Warning, "
				 "'ad-hoc' yield blocking (mutexes?) is not "
				 "suitable for disk I/O! (TID %d)\n", a->tid);
			return false;
		}
	);
	/* Now do for each *non*-runnable agent... */
	Q_FOREACH(a, &ls->sched.dq, nobe) {
		if (a->action.disk_io) {
			lsprintf(CHOICE, "TID %d blocked on disk I/O. "
				 "Allowing idle to run.\n", a->tid);
			return false;
		}
	}
	return true;
}

#define IS_IDLE(ls, a)							\
	(TID_IS_IDLE((a)->tid) &&					\
	 BUG_ON_THREADS_WEDGED != 0 && (ls)->test.test_ever_caused &&	\
	 (ls)->test.start_population != (ls)->sched.most_agents_ever)

/* Attempting to track whether threads are "blocked" based on when they call
 * yield() while inside mutex_lock() is great for avoiding the expensive
 * yield-loop-counting heuristic, it can produce some false positive deadlocks
 * when a thread's blocked-on-addr doesn't get unset at the right time. A good
 * example is when mutex_lock actually deschedule()s, and has a little-lock
 * inside that yields. We can't know (without annotations) that we need to
 * unset contenders' blocked-on-addrs when e.g. little_lock_unlock() is called
 * at the end of mutex_lock().
 *
 * The tradeoff with this knob is how long FAB traces are for deadlock reports,
 * versus how many benign repetitions an adversarial program must contain in
 * order to trigger a false positive report despite this cleverness. */
#define DEADLOCK_FP_MAX_ATTEMPTS 16
static bool try_avoid_fp_deadlock(struct ls_state *ls, struct agent **result) {
	/* The counter is reset every time we backtrack, but it's never reset
	 * during a single branch. This gives some notion of progress, so we
	 * won't just try this strategy forever in a real deadlock situation. */
	if (ls->sched.deadlock_fp_avoidance_count == DEADLOCK_FP_MAX_ATTEMPTS) {
		return false;
	}

	ls->sched.deadlock_fp_avoidance_count++;

	bool found_one = false;
	struct agent *a;
	/* Doesn't matter which thread we choose; take whichever is latest in
	 * this loop. But we need to wake all of them, not knowing which was
	 * "faking it". If it's truly deadlocked, they'll all block again. */
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		// TODO: Also support yield-loop blocking here (issue #75).
		if (a->user_blocked_on_addr != -1) {
			assert(!IS_IDLE(ls, a) && "That's weird.");
			lsprintf(DEV, "I thought TID %d was blocked on 0x%x, "
				 "but I could be wrong!\n",
				 a->tid, a->user_blocked_on_addr);
			a->user_blocked_on_addr = -1;
			*result = a;
			found_one = true;
		} else if (agent_is_user_yield_blocked(&a->user_yield)) {
			assert(!IS_IDLE(ls, a) && "That's weird.");
			lsprintf(DEV, "I thought TID %d was blocked yielding "
				 "(ylc %u), but I could be wrong!\n",
				 a->tid, a->user_yield.loop_count);
			a->user_yield.loop_count = 0;
			a->user_yield.blocked = false;
			*result = a;
			found_one = true;
		}
	);
	return found_one;
}

/* Returns true if a thread was chosen. If true, sets 'target' (to either the
 * current thread or any other thread), and sets 'our_choice' to false if
 * somebody else already made this choice for us, true otherwise. */
bool arbiter_choose(struct ls_state *ls, struct agent *current,
		    struct agent **result, bool *our_choice)
{
	struct agent *a;
	unsigned int count = 0;
	bool current_is_legal_choice = false;

	/* We shouldn't be asked to choose if somebody else already did. */
	assert(Q_GET_SIZE(&ls->arbiter.choices) == 0);

	lsprintf(DEV, "Available choices: ");

	/* Count the number of available threads. */
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		if (!BLOCKED(a) && !IS_IDLE(ls, a)) {
			print_agent(DEV, a);
			printf(DEV, " ");
			count++;
			if (a == current) {
				current_is_legal_choice = true;
			}
		}
	);

//#define CHOOSE_RANDOMLY
#ifdef CHOOSE_RANDOMLY
	// with given odds, will make the "forwards" choice.
	const int numerator   = 19;
	const int denominator = 20;
	if (rand64(&ls->rand) % denominator < numerator) {
		count = 1;
	}
#else
	if (EXPLORE_BACKWARDS == 0) {
		count = 1;
	}
#endif

	if (current_is_legal_choice &&
	    (agent_has_yielded(&current->user_yield) ||
	     agent_has_xchged(&ls->user_sync))) {
		printf(DEV, "- Must run yielding thread %d\n", current->tid);
		*result = current;
		*our_choice = true;
		return true;
	}

	/* Find the count-th thread. */
	unsigned int i = 0;
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		if (!BLOCKED(a) && !IS_IDLE(ls, a) && ++i == count) {
			printf(DEV, "- Figured I'd look at TID %d next.\n",
			       a->tid);
			*result = a;
			*our_choice = true;
			return true;
		}
	);

	printf(DEV, "... none?\n");

	/* No runnable threads. Is this a bug, or is it expected? */
	if (report_deadlock(ls)) {
		if (try_avoid_fp_deadlock(ls, result)) {
			lsprintf(CHOICE, COLOUR_BOLD COLOUR_YELLOW
				 "WARNING: System is apparently deadlocked! "
				 "Let me just try one thing. See you soon.\n");
			*our_choice = true;
			return true;
		} else {
			FOUND_A_BUG(ls, "Deadlock -- no threads are runnable!\n");
			return false;
		}
	} else {
		lsprintf(DEV, "Deadlock -- no threads are runnable!\n");
		return false;
	}
}
