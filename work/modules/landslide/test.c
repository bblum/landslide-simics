/**
 * @file test.c
 * @brief routine for managing test case running / kernel execution lifetime
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define MODULE_NAME "TEST"
#define MODULE_COLOUR COLOUR_CYAN

#include "common.h"
#include "kernel_specifics.h"
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
static bool anybody_alive(conf_object_t *cpu, struct test_state *t,
			  struct sched_state *s)
{
	struct agent *shell;

	if (t->test_ever_caused) {
		if (t->start_population == s->most_agents_ever) {
			/* Then the shell hasn't even spawned it yet. */
			return true;
		} else if (t->start_population != s->num_agents) {
			/* Shell's descendants are still alive. */
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
	if (kern_scheduler_locked(cpu) || !interrupts_enabled(cpu)) {
		return true;
	}

	/* */
	if ((shell = agent_by_tid_or_null(&s->rq, kern_get_shell_tid())) ||
	    (shell = agent_by_tid_or_null(&s->dq, kern_get_shell_tid()))) {
		if (shell->action.readlining) {
			// FIXME: some kernels might have an idle thread
			return (Q_GET_SIZE(&s->rq) != 0 ||
				Q_GET_SIZE(&s->sq) != 0);
		} else {
			return true;
		}
	}

	/* If we get here, the shell wasn't even created yet..! */
	return true;
}

/* returns true if the state changed. */
bool test_update_state(conf_object_t *cpu, struct test_state *t,
		       struct sched_state *s)
{
	if (anybody_alive(cpu, t, s)) {
		if (!t->test_is_running) {
			lsprintf("a test appears to be starting - ");
			print_qs(s);
			printf("\n");
			t->test_is_running = true;
			return true;
		}
	} else {
		if (t->test_is_running) {
			lsprintf("a test appears to be ending - ");
			print_qs(s);
			printf("\n");
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

bool cause_test(conf_object_t *kbd, struct test_state *t, struct sched_state *s,
		const char *test_string)
{
	if (t->test_is_running || t->current_test) {
		lsprintf("can't run \"%s\" with another test running\n",
			 test_string);
		return false;
	}

	/* save the test string */
	t->current_test = MM_STRDUP(test_string);
	assert(t->current_test && "couldn't allocate memory for test string");

	/* feed input */
	for (int i = 0; i < strlen(test_string); i++) {
		cause_keypress(kbd, test_string[i]);
	}
	if (test_string[strlen(test_string)-1] != '\n') {
		cause_keypress(kbd, '\n');
		lsprintf("caused test %s\n", test_string);
	} else {
		lsprintf("caused test %s", test_string);
	}

	t->test_ever_caused = true;
	// FIXME: this might fail on some valid kernels? not sure how to fix
	assert(s->num_agents == s->most_agents_ever &&
	       "trying to start a test after somebody has already died?");
	t->start_population = s->num_agents;

	return true;
}
