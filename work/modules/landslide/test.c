/**
 * @file test.c
 * @brief routine for managing test case running / kernel execution lifetime
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define MODULE_NAME "TEST"
#define MODULE_COLOUR COLOUR_CYAN

#include "common.h"
#include "found_a_bug.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "schedule.h"
#include "test.h"
#include "variable_queue.h"
#include "x86.h"

/******************************************************************************
 * Common to all kernels
 ******************************************************************************/

void test_init(struct test_state *t)
{
	t->test_is_running  = false;
	t->test_ended       = false;
	t->test_ever_caused = false;
	t->current_test     = NULL;
}

bool cause_test(conf_object_t *kbd, struct test_state *t, struct ls_state *ls,
		const char *test_string)
{
	if (t->test_is_running || t->current_test != NULL) {
		lsprintf(INFO, "can't run \"%s\" with another test running\n",
			 test_string);
		return false;
	}

	/* save the test string */
	t->current_test = MM_XSTRDUP(test_string);

#ifndef PINTOS_KERNEL
	/* feed input */
	for (int i = 0; i < strlen(test_string); i++) {
		cause_keypress(kbd, test_string[i]);
	}
	if (test_string[strlen(test_string)-1] != '\n') {
		cause_keypress(kbd, '\n');
		lsprintf(BRANCH, "Beginning test %s\n", test_string);
	} else {
		lsprintf(BRANCH, "Beginning test %s", test_string);
	}
#endif

	t->test_ever_caused = true;

	/* Record how many people are alive at the start of the test */
	if (ls->sched.num_agents != ls->sched.most_agents_ever) {
	       lskprintf(BUG, "WARNING: somebody died before test started!\n");
	       ls->sched.most_agents_ever = ls->sched.num_agents;
	}
	t->start_population = ls->sched.num_agents;
	lsprintf(DEV, "test startpop... sched state: ");
	print_qs(DEV, &ls->sched);
	printf(DEV, "\n");

	/* Record the size of the heap at the start of the test */
	t->start_kern_heap_size = ls->kern_mem.heap_size;
	t->start_user_heap_size = ls->user_mem.heap_size;

	return true;
}

#ifdef PINTOS_KERNEL

/******************************************************************************
 * Pintos
 ******************************************************************************/

#define REASON(str) do { if (chatty) { lsprintf(DEV, "%s\n", str); } } while (0)
bool anybody_alive(conf_object_t *cpu, struct test_state *t,
		   struct sched_state *s, bool chatty)
{
	if (Q_GET_SIZE(&s->rq) != 0) {
		REASON("Somebody's alive -- runqueue is populated.");
		return true;
	} else if (Q_GET_SIZE(&s->sq) != 0) {
		REASON("Somebody's alive -- sleep queue is populated.");
		return true;
	} else if (Q_GET_SIZE(&s->dq) != 0) {
		struct agent *head = Q_GET_HEAD(&s->dq);
		struct agent *tail = Q_GET_TAIL(&s->dq);
		if (Q_GET_SIZE(&s->dq) == 1 && TID_IS_IDLE(head->tid)) {
			REASON("Nobody's alive -- only idle remains.");
			return false;
		} else if (t->test_ended && Q_GET_SIZE(&s->dq) == 2 &&
			   ((TID_IS_IDLE(head->tid) && TID_IS_INIT(tail->tid)) ||
			    (TID_IS_IDLE(tail->tid) && TID_IS_INIT(head->tid)))) {
			REASON("Somebody's alive -- only idle and init remain.");
			return false;
		} else {
			REASON("Somebody's alive -- a non-idle thread exists.");
			return true;
		}
	} else {
		REASON("Nobody's alive -- no threads exist (idle vanished?!).");
		return false;
	}
}

bool test_update_state(struct ls_state *ls)
{
	struct test_state *t = &ls->test;
	if (ls->eip == GUEST_RUN_TASK_ENTER) {
		lsprintf(DEV, "a test appears to be starting - ");
		print_qs(DEV, &ls->sched);
		printf(DEV, "\n");
		return true;
	} else if (ls->eip == GUEST_RUN_TASK_EXIT) {
		t->test_ended = true;
		lsprintf(DEV, "a test appears to be ending - ");
		print_qs(DEV, &ls->sched);
		printf(DEV, "\n");
		return false; /* Wait for all threads to quiesce. */
	} else if (t->test_ended && !anybody_alive(ls->cpu0, t, &ls->sched, false)) {
		lsprintf(DEV, "threads have quiesced; ok to rewind\n");
		return true;
	} else {
		/* No change. */
		return false;
	}
}

#else

/******************************************************************************
 * Pebbles
 ******************************************************************************/

static bool unexpected_idle()
{
	// XXX: Thread ls through instead of this grossness.
	struct ls_state *ls = (struct ls_state *)SIM_get_object("landslide0");
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED
		 "Init is still runnable but the kernel is idling.\n");
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "The kernel must not run the "
		 "idle loop when other work can be done.\n");
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Scheduler state: ");
	print_scheduler_state(ALWAYS, &ls->sched);
	printf(ALWAYS, "\n");

	if (ls->save.total_jumps > 0) {
		/* ...a race? Give a full report instead of a terse complaint. */
		FOUND_A_BUG(ls, "Kernel is unexpectedly idling.");
	}
	assert(0);
	return false;
}

// TODO: some way of telling when actually ... use readline to know
#define REASON(str) do { if (chatty) { lsprintf(DEV, "%s\n", str); } } while (0)
bool anybody_alive(conf_object_t *cpu, struct test_state *t,
		   struct sched_state *s, bool chatty)
{
	struct agent *shell;

	if (t->test_ever_caused) {
		if (t->start_population == s->most_agents_ever) {
			/* Then the shell hasn't even spawned it yet. */
			REASON("Somebody's alive: Test caused & shell hasn't "
			       "spawned yet.");
			return true;
		} else if (t->start_population != s->num_agents) {
			/* Shell's descendants are still alive. */
			REASON("Somebody's alive: Test caused & shell's "
			       "descendants are alive.");
			assert(t->start_population < s->num_agents);
			return true;
		}
	}

	/* Now we are either before the beginning of the test case, or waiting
	 * for the shell and init to clean up the last remains. Either way, wait
	 * for shell and init to finish switching back and forth until both of
	 * them are suitably blocked. */

	/* In atomic scheduler paths, both threads might be off the runqueue
	 * (i.e., one going to sleep and switching to the other). Since we
	 * assume the scheduler is sane, this condition should hold then. */
	if (!kern_ready_for_timer_interrupt(cpu) || !interrupts_enabled(cpu)) {
		if (chatty) {
			if (!kern_ready_for_timer_interrupt(cpu)) {
				REASON("Somebody's alive: Preemption not enabled.");
			} else {
				REASON("Somebody's alive: Interrupts off.");
			}
		}
		return true;
	}

	/* Figure out what the shell is doing - running or waiting? */
	assert(kern_has_shell() && "Pebbles kernel must have shell!");
	if ((shell = agent_by_tid_or_null(&s->rq, kern_get_shell_tid())) ||
	    (shell = agent_by_tid_or_null(&s->dq, kern_get_shell_tid()))) {
		if (shell->action.readlining) {
			if (kern_has_idle()) {
				if (s->cur_agent->tid != kern_get_idle_tid()) {
					REASON("Somebody's alive: shell "
					       "readlining but idle not "
					       "running");
					return true;
				} else if (agent_by_tid_or_null(&s->rq, kern_get_init_tid()) != NULL) {
					return unexpected_idle();
				} else {
					REASON("Nobody alive: shell readlining "
					       "& idle running.");
					return false;
				}
			} else {
				// FIXME: account for current_extra_runnable
				if (Q_GET_SIZE(&s->rq) != 0 ||
				    Q_GET_SIZE(&s->sq) != 0) {
					REASON("Somebody's alive: shell "
					       "readlining but somebody's "
					       "runnable.");
					return true;
				} else {
					REASON("Nobody alive: shell readlining "
					       "& nobody runnable.");
					return false;
				}
			}
		} else {
			REASON("Somebody's alive: shell not readlining.");
			return true;
		}
	}

	/* If we get here, the shell wasn't even created yet..! */
	REASON("Somebody's alive: shell doesn't even exist.");
	return true;
}
#undef REASON

/* returns true if the state changed. */
bool test_update_state(struct ls_state *ls)
{
	struct test_state *t = &ls->test;
	if (anybody_alive(ls->cpu0, t, &ls->sched, false)) {
		if (!t->test_is_running) {
			lsprintf(DEV, "a test appears to be starting - ");
			print_qs(DEV, &ls->sched);
			printf(DEV, "\n");
			t->test_is_running = true;
			return true;
		}
	} else {
		if (t->test_is_running) {
			lsprintf(DEV, "a test appears to be ending - ");
			print_qs(DEV, &ls->sched);
			printf(DEV, "\n");
			if (t->current_test) {
				MM_FREE(t->current_test);
				t->current_test = NULL;
			}
			t->test_is_running = false;
			return true;
		}
	}
	return false;
}

#endif /* ndef PINTOS_KERNEL */
