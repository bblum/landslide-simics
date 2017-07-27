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
#include "pp.h"
#include "rand.h"
#include "schedule.h"
#include "user_specifics.h"
#include "user_sync.h"
#include "x86.h"

void arbiter_init(struct arbiter_state *r)
{
	Q_INIT_HEAD(&r->choices);
}

void arbiter_append_choice(struct arbiter_state *r, unsigned int tid, bool txn, unsigned int xabort_code)
{
	struct choice *c = MM_XMALLOC(1, struct choice);
	c->tid = tid;
	c->txn = txn;
	c->xabort_code = xabort_code;
	Q_INSERT_FRONT(&r->choices, c, nobe);
}

bool arbiter_pop_choice(struct arbiter_state *r, unsigned int *tid, bool *txn, unsigned int *xabort_code)
{
	struct choice *c = Q_GET_TAIL(&r->choices);
	if (c) {
		lsprintf(DEV, "using requested tid %d\n", c->tid);
		Q_REMOVE(&r->choices, c, nobe);
		*tid = c->tid;
		*txn = c->txn;
		*xabort_code = c->xabort_code;
		MM_FREE(c);
		return true;
	} else {
		return false;
	}
}

#define ASSERT_ONE_THREAD_PER_PP(ls) do {					\
		assert((/* root pp not created yet */				\
		        (ls)->save.next_tid == -1 ||				\
		        /* thread that was chosen is still running */		\
		        (ls)->save.next_tid == (ls)->sched.cur_agent->tid) &&	\
		       "One thread per preemption point invariant violated!");	\
	} while (0)

bool arbiter_interested(struct ls_state *ls, bool just_finished_reschedule,
			bool *voluntary, bool *need_handle_sleep, bool *data_race,
			bool *xbegin)
{
	*voluntary = false;
	*need_handle_sleep = false;
	*data_race = false;
	*xbegin = false;

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
		   // NB. The use of KERNEL_MEMORY here used to be !testing_userspace.
		   // I needed to change it to implement preempt-everywhere mode,
		   // to handle the case of userspace shms in deschedule() syscall.
		   // Not entirely sure of all implications of this change.
		   && ((!KERNEL_MEMORY(ls->eip) && user_within_functions(ls)) ||
		      (KERNEL_MEMORY(ls->eip) && kern_within_functions(ls)))
#endif
		   && !ls->sched.cur_agent->action.user_txn) {
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
			// FIXME: should be equivalent to making a syscall in a txn
			assert(!ls->sched.cur_agent->action.user_txn && "txn must abort");
			return true;
#ifndef PINTOS_KERNEL
		} else if (!check_user_address_space(ls)) {
			return false;
#endif
		} else if ((user_mutex_lock_entering(ls->cpu0, ls->eip, &mutex_addr) ||
			    user_mutex_unlock_exiting(ls->eip)) &&
			   user_within_functions(ls)) {
			ASSERT_ONE_THREAD_PER_PP(ls);
			// FIXME: can we skip this PP without violating soundness?
			assert(!ls->sched.cur_agent->action.user_txn && "is this ok??");
			return true;
		} else if (user_xbegin_entering(ls->eip) ||
			   user_xend_entering(ls->eip)) {
			/* Have to disrespect within functions to properly
			 * respect htm-blocking if there's contention. */
			ASSERT_ONE_THREAD_PER_PP(ls);
			*xbegin = user_xbegin_entering(ls->eip);
			return true;
		} else {
			return false;
		}
	/* kernel-mode-only preemption points */
#ifdef PINTOS_KERNEL
	} else if ((ls->eip == GUEST_SEMA_DOWN_ENTER || ls->eip == GUEST_SEMA_UP_EXIT) && kern_within_functions(ls)) {
		ASSERT_ONE_THREAD_PER_PP(ls);
		return true;
	} else if ((ls->eip == GUEST_CLI_ENTER || ls->eip == GUEST_STI_EXIT) &&
		   !ls->sched.cur_agent->action.kern_mutex_locking &&
		   !ls->sched.cur_agent->action.kern_mutex_unlocking &&
		   kern_within_functions(ls)) {
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
#define DEADLOCK_FP_MAX_ATTEMPTS 128
static bool try_avoid_fp_deadlock(struct ls_state *ls, bool voluntary,
				  struct agent **result) {
	/* The counter is reset every time we backtrack, but it's never reset
	 * during a single branch. This gives some notion of progress, so we
	 * won't just try this strategy forever in a real deadlock situation. */
	if (ls->sched.deadlock_fp_avoidance_count == DEADLOCK_FP_MAX_ATTEMPTS) {
		return false;
	}

	ls->sched.deadlock_fp_avoidance_count++;

	bool found_one = false;
	struct agent *a;

	/* We must prioritize trying ICB-blocked threads higher than yield/xchg-
	 * blocked ones, because ICB-blocked threads won't get run "on their
	 * own" at subsequent PPs; rather we must force it immediately here.
	 * In fact, we must check *all* threads for being ICB-blocked before
	 * checking *any* for other kinds of blockage, so that we don't awaken
	 * the latter type unnecessarily (resulting in infinite subtrees). */
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		if (ICB_BLOCKED(&ls->sched, ls->icb_bound, voluntary, a)) {
			assert(!IS_IDLE(ls, a) && "That's weird.");
			/* a thread could be multiple types of maybe-blocked at
			 * once. skip those for now; prioritizing ICB-blocked
			 * ones that are definitely otherwise runnable. */
			if (a->user_blocked_on_addr == -1 &&
			    !agent_is_user_yield_blocked(&a->user_yield)) {
				lsprintf(DEV, "I thought TID %d was ICB-blocked "
					 "(bound %u), but maybe preempting is "
					 "needed here for  correctness!\n",
					 a->tid, ls->icb_bound);
				*result = a;
				found_one = true;
			}
		}
	);

	if (found_one) {
		/* Found ICB-blocked thread to wake. Return early. */
		return found_one;
	}

	/* Doesn't matter which thread we choose; take whichever is latest in
	 * this loop. But we need to wake all of them, not knowing which was
	 * "faking it". If it's truly deadlocked, they'll all block again. */
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
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
bool arbiter_choose(struct ls_state *ls, struct agent *current, bool voluntary,
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
		if (!BLOCKED(a) && !IS_IDLE(ls, a) &&
		    !HTM_BLOCKED(&ls->sched, a) &&
		    !ICB_BLOCKED(&ls->sched, ls->icb_bound, voluntary, a)) {
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
#ifdef ICB
	STATIC_ASSERT(false && "ICB and CHOOSE_RANDOMLY are incompatible");
#endif
	// with given odds, will make the "forwards" choice.
	const int numerator   = 19;
	const int denominator = 20;
	if (rand64(&ls->rand) % denominator < numerator) {
		count = 1;
	}
#else
	if (EXPLORE_BACKWARDS == 0) {
		count = 1;
	} else {
#ifdef ICB
		assert(false && "For ICB, EXPLORE_BACKWARDS must be 0.");
#endif
	}
#endif

	if (agent_has_yielded(&current->user_yield) ||
	    agent_has_xchged(&ls->user_sync)) {
		if (current_is_legal_choice) {
			printf(DEV, "- Must run yielding thread %d\n",
			       current->tid);
			/* NB. this will be last_agent when yielding. */
			*result = current;
			*our_choice = true;
			/* Preemption count doesn't increase. */
			return true;
		} else if (!agent_is_user_yield_blocked(&current->user_yield)) {
			/* Something funny happened, causing the thread to get
			 * ACTUALLY blocked before finishing yield-blocking. Any
			 * false-positive yield senario could trigger this. */
			assert(!current->user_yield.blocked);
			current->user_yield.loop_count = 0;
		} else {
			/* Normal case of blocking with TOO MANY YIELDS. */
		}
	}

	/* Find the count-th thread. */
	unsigned int i = 0;
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		if (!BLOCKED(a) && !IS_IDLE(ls, a) &&
		    !HTM_BLOCKED(&ls->sched, a) &&
		    !ICB_BLOCKED(&ls->sched, ls->icb_bound, voluntary, a) &&
		    ++i == count) {
			printf(DEV, "- Figured I'd look at TID %d next.\n",
			       a->tid);
			*result = a;
			*our_choice = true;
			/* Should preemption counter increase for ICB? */
			// FIXME: actually, I'm pretty sure this is dead code.
			// Given EXPLORE_BACKWARDS=0, don't we always choose
			// either the cur agent or the last agent?
			if (!NO_PREEMPTION_REQUIRED(&ls->sched, voluntary, a)) {
				ls->sched.icb_preemption_count++;
				lsprintf(DEV, "Switching to TID %d will count "
					 "as a preemption for ICB.\n", a->tid);
			}
			return true;
		}
	);

	printf(DEV, "... none?\n");

	/* No runnable threads. Is this a bug, or is it expected? */
	if (report_deadlock(ls)) {
		if (try_avoid_fp_deadlock(ls, voluntary, result)) {
			lsprintf(CHOICE, COLOUR_BOLD COLOUR_YELLOW
				 "WARNING: System is apparently deadlocked! "
				 "Let me just try one thing. See you soon.\n");
			*our_choice = true;
			/* Special case. Bypass preemption count; this mechanism
			 * is needed for correctness, so ICB can't interfere. */
			return true;
		} else {
			if (voluntary) {
				save_setjmp(&ls->save, ls, -1, true,
					    true, true, -1, true, false);
			}
			lsprintf(DEV, "ICB count %u bound %u\n",
				 ls->sched.icb_preemption_count, ls->icb_bound);
			FOUND_A_BUG(ls, "Deadlock -- no threads are runnable!\n");
			return false;
		}
	} else {
		lsprintf(DEV, "Deadlock -- no threads are runnable!\n");
		return false;
	}
}
