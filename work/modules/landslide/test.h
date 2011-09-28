/**
 * @file test.h
 * @brief routine for managing test case running / kernel execution lifetime
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_TEST_H
#define __LS_TEST_H

#include <simics/api.h>

struct sched_state;

struct test_state {
	bool test_is_running;
	bool test_ever_caused;
	char *current_test;
};

void test_init(struct test_state *);
bool test_update_state(struct test_state *, struct sched_state *);
bool cause_test(struct test_state *, conf_object_t *kbd, const char *);

#endif
