/*
  landslide.c - A Module for Simics which provides yon Hax and Sploits

  Copyright 1998-2009 Virtutech AB
  
  The contents herein are Source Code which are a subset of Licensed
  Software pursuant to the terms of the Virtutech Simics Software
  License Agreement (the "Agreement"), and are being distributed under
  the Agreement.  You should have received a copy of the Agreement with
  this Licensed Software; if not, please contact Virtutech for a copy
  of the Agreement prior to using this Licensed Software.
  
  By using this Source Code, you agree to be bound by all of the terms
  of the Agreement, and use of this Source Code is subject to the terms
  the Agreement.
  
  This Source Code and any derivatives thereof are provided on an "as
  is" basis.  Virtutech makes no warranties with respect to the Source
  Code or any derivatives thereof and disclaims all implied warranties,
  including, without limitation, warranties of merchantability and
  fitness for a particular purpose and non-infringement.

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simics/api.h>
#include <simics/arch/x86.h>

// XXX: this header lacks guards, so it must be after the other includes.
#include "trace.h"

#define MODULE_NAME "LANDSLIDE"
#define MODULE_COLOUR COLOUR_DARK COLOUR_MAGENTA

#include "common.h"
#include "explore.h"
#include "estimate.h"
#include "found_a_bug.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "memory.h"
#include "messaging.h"
#include "rand.h"
#include "save.h"
#include "test.h"
#include "tree.h"
#include "user_specifics.h"
#include "x86.h"

struct ls_state *new_landslide()
{
	// TODO: have some global lock file to ensure not called twice
	struct ls_state *ls = MM_XMALLOC(1, struct ls_state);
	ls->trigger_count = 0;
	ls->absolute_trigger_count = 0;

	ls->cpu0 = SIM_get_object("cpu0");
	assert(ls->cpu0 != NULL && "failed to find cpu");
	ls->kbd0 = SIM_get_object("kbd0");
	assert(ls->kbd0 != NULL && "failed to find keyboard");

	sched_init(&ls->sched);
	arbiter_init(&ls->arbiter);
	save_init(&ls->save);
	test_init(&ls->test);
	mem_init(ls);
	user_sync_init(&ls->user_sync);
	rand_init(&ls->rand);
	messaging_init(&ls->mess);

	ls->cmd_file = NULL;
	ls->html_file = NULL;
	ls->just_jumped = false;

	lsprintf(ALWAYS, "welcome to landslide.\n");

	return ls;
}

/******************************************************************************
 * pebbles system calls
 ******************************************************************************/

#define CASE_SYSCALL(num, name) \
	case (num): printf(CHOICE, "%s()\n", (name)); break

static void check_user_syscall(struct ls_state *ls)
{
	if (READ_BYTE(ls->cpu0, ls->eip) != OPCODE_INT) {
		ls->sched.cur_agent->most_recent_syscall = 0;
		return;
	}

	lsprintf(CHOICE, "TID %d makes syscall ", ls->sched.cur_agent->tid);

	int number = OPCODE_INT_ARG(ls->cpu0, ls->eip);
	switch(number) {
		CASE_SYSCALL(FORK_INT, "fork");
		CASE_SYSCALL(EXEC_INT, "exec");
		CASE_SYSCALL(WAIT_INT, "wait");
		CASE_SYSCALL(YIELD_INT, "yield");
		CASE_SYSCALL(DESCHEDULE_INT, "deschedule");
		CASE_SYSCALL(MAKE_RUNNABLE_INT, "make_runnable");
		CASE_SYSCALL(GETTID_INT, "gettid");
		CASE_SYSCALL(NEW_PAGES_INT, "new_pages");
		CASE_SYSCALL(REMOVE_PAGES_INT, "remove_pages");
		CASE_SYSCALL(SLEEP_INT, "sleep");
		CASE_SYSCALL(GETCHAR_INT, "getchar");
		CASE_SYSCALL(READLINE_INT, "readline");
		CASE_SYSCALL(PRINT_INT, "print");
		CASE_SYSCALL(SET_TERM_COLOR_INT, "set_term_color");
		CASE_SYSCALL(SET_CURSOR_POS_INT, "set_cursor_pos");
		CASE_SYSCALL(GET_CURSOR_POS_INT, "get_cursor_pos");
		CASE_SYSCALL(THREAD_FORK_INT, "thread_fork");
		CASE_SYSCALL(GET_TICKS_INT, "get_ticks");
		CASE_SYSCALL(MISBEHAVE_INT, "misbehave");
		CASE_SYSCALL(HALT_INT, "halt");
		CASE_SYSCALL(LS_INT, "ls");
		CASE_SYSCALL(TASK_VANISH_INT, "task_vanish");
		CASE_SYSCALL(SET_STATUS_INT, "set_status");
		CASE_SYSCALL(VANISH_INT, "vanish");
		CASE_SYSCALL(CAS2I_RUNFLAG_INT, "cas2i_runflag");
		CASE_SYSCALL(SWEXN_INT, "swexn");
		default:
			printf(CHOICE, "((unknown 0x%x))\n", number);
			break;
	}

	ls->sched.cur_agent->most_recent_syscall = number;
}
#undef CASE_SYSCALL

#define TRIPLE_FAULT_EXCEPTION 1024 /* special simics value */

static const char *exception_names[] = {
	"divide error",
	"single-step exception",
	"nmi",
	"breakpoint",
	"overflow",
	"bounds check",
	"invalid opcode",
	"coprocessor not available",
	"double fault",
	"coprocessor segment overrun",
	"invalid tss",
	"segment not present",
	"stack segment exception",
	"general protection fault",
	"page fault",
	"<reserved>",
	"coprocessor error",
};

static void check_exception(struct ls_state *ls, int number)
{
	if (number < ARRAY_SIZE(exception_names)) {
		ls->sched.cur_agent->most_recent_syscall = number;
		lsprintf(CHOICE, "Exception #%d (%s) taken at ",
			 number, exception_names[number]);
		print_eip(CHOICE, ls->eip);
		printf(CHOICE, "\n");
	} else if (number == TRIPLE_FAULT_EXCEPTION) {
		FOUND_A_BUG(ls, "Triple fault!");
	} else {
		lsprintf(INFO, "Exception #%d (syscall or interrupt) @ ", number);
		print_eip(INFO, ls->eip);
		printf(INFO, "\n");
	}
}

/******************************************************************************
 * miscellaneous bug detection metrics
 ******************************************************************************/

/* How many transitions deeper than the average branch should this branch be
 * before we call it stuck? */
#define PROGRESS_DEPTH_FACTOR 20
/* How many branches should we already have explored before we have a stable
 * enough average to judge an abnormally deep branch? FIXME see below */
#define PROGRESS_MIN_BRANCHES 20
/* How many times more instructions should a given transition be than the
 * average previous transition before we proclaim it stuck? */
#define PROGRESS_TRIGGER_FACTOR 2000

static void wrong_panic(struct ls_state *ls, const char *panicked, const char *expected)
{
	lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW "********************************\n");
	lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW
		 "The %s panicked during a %sspace test. This shouldn't happen.\n",
		 panicked, expected);
	lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW
		 "This is more likely a problem with the test configuration,\n");
	lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW
		 "or a reference kernel bug, than a bug in your code.\n");
	lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW "********************************\n");
	FOUND_A_BUG(ls, "Unexpected %s panic during %sspace test", panicked, expected);
	assert(0 && "wrong panic");
}

static bool ensure_progress(struct ls_state *ls)
{
	char *buf;
	unsigned int tid = ls->sched.cur_agent->tid;

	if (kern_panicked(ls->cpu0, ls->eip, &buf)) {
		if (testing_userspace()) {
			wrong_panic(ls, "kernel", "user");
		} else {
			FOUND_A_BUG(ls, "KERNEL PANIC: %s", buf);
		}
		MM_FREE(buf);
		return false;
	} else if (user_panicked(ls->cpu0, ls->eip, &buf) &&
		   tid != kern_get_init_tid() && tid != kern_get_shell_tid() &&
		   !(kern_has_idle() && tid == kern_get_idle_tid())) {
		if (testing_userspace()) {
			FOUND_A_BUG(ls, "USERSPACE PANIC: %s", buf);
		} else {
			wrong_panic(ls, "user", "kernel");
		}
		MM_FREE(buf);
		return false;
	} else if (user_report_end_fail(ls->cpu0, ls->eip)) {
		FOUND_A_BUG(ls, "User test program reported failure!");
#ifdef GUEST_PF_HANDLER
	} else if (ls->eip == GUEST_PF_HANDLER) {
		unsigned int from_eip = READ_STACK(ls->cpu0, 1);
		lsprintf(DEV, "page fault from ");
		print_eip(DEV, from_eip);
		printf(DEV, "\n");
		/* did it come from kernel-space? */
		if (KERNEL_MEMORY(from_eip)) {
			// TODO: say "note to configure, unset PF_HANDLER
			FOUND_A_BUG(ls, "Kernel page faulted!");
			return false;
		}
#endif
	}

	/* Can't check for tight loops 0th branch. If one transition has an
	 * expensive operation like vm_free_pagedir() we don't want to trip on
	 * it; we want to incorporate it into the average. */
	if (ls->save.total_jumps == 0)
		return true;

	/* Have we been spinning since the last choice point? */
	unsigned long most_recent =
		ls->trigger_count - ls->save.current->trigger_count;
	unsigned long average_triggers =
		ls->save.total_triggers / ls->save.total_choices;
	if (most_recent > average_triggers * PROGRESS_TRIGGER_FACTOR) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "%lu instructions since last decision; average %lu\n",
			 most_recent, average_triggers);
		FOUND_A_BUG(ls, "NO PROGRESS (infinite loop?)");
		return false;
	}

	/* FIXME: find a less false-negative way to tell when we have enough
	 * data */
	STATIC_ASSERT(PROGRESS_MIN_BRANCHES > 0); /* prevent div-by-zero */
	if (ls->save.total_jumps < PROGRESS_MIN_BRANCHES)
		return true;

	/* Have we been spinning around a choice point (so this branch would
	 * end up being infinitely deep)? */
	unsigned int average_depth =
		ls->save.depth_total / (1 + ls->save.total_jumps);
	if (ls->save.current->depth > average_depth * PROGRESS_DEPTH_FACTOR) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "Current branch depth %d; average depth %d\n",
			 ls->save.current->depth, average_depth);
		FOUND_A_BUG(ls, "NO PROGRESS (stuck thread(s)?)");
		return false;
	}

	return true;
}

static bool test_ended_safely(struct ls_state *ls)
{
	/* Anything that would indicate failure - e.g. return code... */

	// TODO: find the blocks that were leaked and print stack traces for them
	// TODO: the test could copy the heap to indicate which blocks
	// TODO: do some assert analogous to wrong_panic() for this
	if (ls->test.start_kern_heap_size > ls->kern_mem.heap_size) {
		FOUND_A_BUG(ls, "KERNEL MEMORY LEAK (%d bytes)!",
			    ls->test.start_kern_heap_size - ls->kern_mem.heap_size);
		return false;
	} else if (ls->test.start_user_heap_size > ls->user_mem.heap_size) {
		FOUND_A_BUG(ls, "USER MEMORY LEAK (%d bytes)!",
			    ls->test.start_user_heap_size - ls->user_mem.heap_size);
		return false;
	}

	return true;
}

static void found_no_bug(struct ls_state *ls)
{
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_GREEN
		 "**** Execution tree explored; you survived! ****\n"
		 COLOUR_DEFAULT);
	PRINT_TREE_INFO(DEV, ls);
	SIM_quit(LS_NO_KNOWN_BUG);
}

static void check_should_abort(struct ls_state *ls)
{
	if (should_abort(&ls->mess)) {
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_YELLOW
			 "**** Abort requested by master process. ****\n"
			 COLOUR_DEFAULT);
		PRINT_TREE_INFO(DEV, ls);
		SIM_quit(LS_NO_KNOWN_BUG);
	}
}

void landslide_assert_fail(const char *message, const char *file,
			   unsigned int line, const char *function)
{
	struct ls_state *ls = (struct ls_state *)SIM_get_object("landslide0");
	message_assert_fail(&ls->mess, message, file, line, function);
#ifdef ID_WRAPPER_MAGIC
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "%s:%u: %s: "
		 "Assertion '%s' failed.\n", file, line, function, message);
	SIM_quit(LS_ASSERTION_FAILED);
#else
	/* Don't send SIGABRT to simics if running under the iterative deepening
	 * wrapper -- simics should quit, not drop into a prompt. */
	__assert_fail(message, file, line, function);
#endif
}

/******************************************************************************
 * main entrypoint and time travel logic
 ******************************************************************************/

static bool time_travel(struct ls_state *ls)
{
	/* find where we want to go in the tree, and choose what to do there */
	unsigned int tid;
	struct hax *h = explore(&ls->save, &tid);

	lsprintf(BRANCH, COLOUR_BOLD COLOUR_GREEN "End of branch #%" PRIu64
		 ".\n" COLOUR_DEFAULT, ls->save.total_jumps + 1);
	print_estimates(ls);
	check_should_abort(ls);

	if (h != NULL) {
		assert(!h->all_explored);
		arbiter_append_choice(&ls->arbiter, tid);
		save_longjmp(&ls->save, ls, h);
		return true;
	} else {
		return false;
	}
}

static void check_test_state(struct ls_state *ls)
{
	/* When a test case finishes, break the simulation so the wrapper can
	 * decide what to do. */
	if (test_update_state(ls->cpu0, &ls->test, &ls->sched) &&
	    !ls->test.test_is_running) {
		/* See if it's time to try again... */
		if (ls->test.test_ever_caused) {
			lsprintf(DEV, "test case ended!\n");

			if (DECISION_INFO_ONLY != 0) {
				DUMP_DECISION_INFO(ls);
			} else if (test_ended_safely(ls)) {
				save_setjmp(&ls->save, ls, -1, true, true,
					    false, -1, false);
				if (!time_travel(ls)) {
					found_no_bug(ls);
				}
			}
		} else {
			lsprintf(DEV, "ready to roll!\n");
			SIM_break_simulation(NULL);
		}
	} else {
		ensure_progress(ls);
	}
}

/* Main entry point. Called every instruction, data access, and extensible. */
void landslide_entrypoint(conf_object_t *obj, void *trace_entry)
{
	struct ls_state *ls = (struct ls_state *)obj;
	trace_entry_t *entry = (trace_entry_t *)trace_entry;

	ls->eip = GET_CPU_ATTR(ls->cpu0, eip);

	if (entry->trace_type == TR_Data) {
		if (ls->just_jumped) {
			/* stray access associated with the last instruction
			 * of a past branch. at this point the rest of our
			 * state has already been rewound, so it's too late to
			 * record the access where/when it belongs. */
			return;
		}
		/* mem access - do heap checks, whether user or kernel */
		mem_check_shared_access(ls, entry->pa, entry->va,
					(entry->read_or_write == Sim_RW_Write));
	} else if (entry->trace_type == TR_Exception) {
		check_exception(ls, entry->value.exception);
	} else if (entry->trace_type != TR_Instruction) {
		/* other event (TR_Execute?) - don't care */
	} else {
		if (USER_MEMORY(ls->eip)) {
			check_user_syscall(ls);
		}
		ls->trigger_count++;
		ls->absolute_trigger_count++;

		if (ls->just_jumped) {
			sched_recover(ls);
			ls->just_jumped = false;
		}

		sched_update(ls);
		mem_update(ls);
		check_test_state(ls);
	}
}
