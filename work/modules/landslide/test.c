/**
 * @file test.c
 * @brief routine for managing test case running / kernel execution lifetime
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define MODULE_NAME "TEST"
#define MODULE_COLOUR COLOUR_CYAN

#include "common.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "schedule.h"
#include "test.h"
#include "variable_queue.h"
#include "x86.h"

void test_init(struct test_state *t)
{
	t->test_is_running  = false;
	t->test_ever_caused = false;
	t->current_test     = NULL;
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

	/* */
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
					lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Init is still runnable but the kernel is idling. Please fix.\n");
					lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Scheduler state: ");
					print_qs(ALWAYS, s);
					printf(ALWAYS, "\n");
					assert(0);
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
bool test_update_state(conf_object_t *cpu, struct test_state *t,
		       struct sched_state *s)
{
	if (anybody_alive(cpu, t, s, false)) {
		if (!t->test_is_running) {
			lsprintf(DEV, "a test appears to be starting - ");
			print_qs(DEV, s);
			printf(DEV, "\n");
			t->test_is_running = true;
			return true;
		}
	} else {
		if (t->test_is_running) {
			lsprintf(DEV, "a test appears to be ending - ");
			print_qs(DEV, s);
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

bool cause_test(conf_object_t *kbd, struct test_state *t, struct ls_state *ls,
		const char *test_string)
{
	if (t->test_is_running || t->current_test) {
		lsprintf(INFO, "can't run \"%s\" with another test running\n",
			 test_string);
		return false;
	}

	/* save the test string */
	t->current_test = MM_XSTRDUP(test_string);

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

	t->test_ever_caused = true;

	/* Record how many people are alive at the start of the test */
	if (ls->sched.num_agents != ls->sched.most_agents_ever) {
	       lskprintf(BUG, "WARNING: somebody died before test started!\n");
	       ls->sched.most_agents_ever = ls->sched.num_agents;
	}
	t->start_population = ls->sched.num_agents;

	/* Record the size of the heap at the start of the test */
	t->start_kern_heap_size = ls->kern_mem.heap_size;
	t->start_user_heap_size = ls->user_mem.heap_size;

	return true;
}
