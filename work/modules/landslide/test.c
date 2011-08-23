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
#include "x86.h"

void test_init(struct test_state *t)
{
	t->test_is_running = false;
	t->current_test = NULL;
}

// TODO: some way of telling when actually ... use readline to know
static bool anybody_alive(struct sched_state *s)
{
	/* test running / not running state is determined by:
	 * 1) nobody on the RQ
	 * 2) nobody on the SQ
	 * 3) on the DQ, init and shell only */
	struct agent *a;

	Q_FOREACH(a, &s->rq, nobe) {
		if (a->tid != kern_get_init_tid() &&
		    a->tid != kern_get_shell_tid()) {
			return true;
		}
	}
	Q_FOREACH(a, &s->sq, nobe) {
		if (a->tid != kern_get_init_tid() &&
		    a->tid != kern_get_shell_tid()) {
			return true;
		}
	}
	Q_FOREACH(a, &s->dq, nobe) {
		if (a->tid != kern_get_init_tid() &&
		    a->tid != kern_get_shell_tid()) {
		    	return true;
		}
	}
	return false;
}

/* returns true if the state changed. */
bool test_update_state(struct test_state *t, struct sched_state *s)
{
	if (anybody_alive(s)) {
		if (!t->test_is_running) {
			lsprintf("a test appears to be starting\n");
			t->test_is_running = true;
			return true;
		}
	} else {
		if (t->test_is_running) {
			lsprintf("a test appears to be ending\n");
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

bool test_is_running(struct test_state *t)
{
	return t->test_is_running;
}

const char *test_get_test(struct test_state *t)
{
	return t->current_test;
}

bool cause_test(struct test_state *t, conf_object_t *kbd, const char *test_string)
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
	}

	return true;
}
