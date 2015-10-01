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
#include "html.h"
#include "kernel_specifics.h"
#include "kspec.h"
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
	if (ls->instruction_text[0] != OPCODE_INT) {
		ls->sched.cur_agent->most_recent_syscall = 0;
		return;
	}

	lsprintf(CHOICE, "TID %d makes syscall ", ls->sched.cur_agent->tid);

	unsigned int number = OPCODE_INT_ARG(ls->cpu0, ls->eip);
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
/* Before this many branches are explored, we are less confident in the above
 * number, and will scale it up by this exponent for each lacking branch. */
#define PROGRESS_CONFIDENT_BRANCHES 20
#define PROGRESS_BRANCH_UNCERTAINTY_EXPONENT 1.2
/* How many times more instructions should a given transition be than the
 * average previous transition before we proclaim it stuck? */
#define PROGRESS_TRIGGER_FACTOR 2000
#define PROGRESS_AGGRESSIVE_TRIGGER_FACTOR 100

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
	char buf[BUF_SIZE];
	int len = scnprintf(buf, BUF_SIZE,
			    "Unexpected %s panic during %sspace test",
			    panicked, expected);
	FOUND_A_BUG_HTML_INFO(ls, buf, len, html_env,
		HTML_PRINTF(html_env, "This shouldn't happen. This is more "
			    "likely a problem with the test configuration, "
			    "or a reference kernel bug, than a bug in your "
			    "own code." HTML_NEWLINE);
	);
	assert(0 && "wrong panic");
}

static bool check_infinite_loop(struct ls_state *ls, char *message, unsigned int maxlen)
{
	/* Condition for stricter check. Less likely long-running non-infinite
	 * loops within user sync primitives, so be more aggressive checking
	 * there. However, the past instruction average must be non-0. */
	bool possible_to_check = ls->save.current != NULL &&
		ls->save.total_triggers != 0;
	bool more_aggressive_check = possible_to_check &&
		(ls->save.total_jumps == 0 || /* don't get owned on 0th branch */
		 IN_USER_SYNC_PRIMITIVES(ls->sched.cur_agent));

	/* Can't check for tight loops 0th branch. If one transition has an
	 * expensive operation like vm_free_pagedir() we don't want to trip on
	 * it; we want to incorporate it into the average. */
	if (ls->save.total_jumps == 0 && !possible_to_check) {
		return false;
	}

	/* Have we been spinning since the last choice point? */
	unsigned long most_recent =
		ls->trigger_count - ls->save.current->trigger_count;
	unsigned long average_triggers =
		ls->save.total_triggers / ls->save.total_choices;
	unsigned long trigger_factor = more_aggressive_check ?
		PROGRESS_AGGRESSIVE_TRIGGER_FACTOR : PROGRESS_TRIGGER_FACTOR;
	unsigned long trigger_thresh = average_triggers * trigger_factor;

	/* print a message at 1% increments */
	if (most_recent > 0 && most_recent % (trigger_thresh / 100) == 0) {
		lsprintf(CHOICE, "progress sense%s: %lu%% (%lu/%lu)\n",
			 more_aggressive_check ? " (aggressive)" : "",
			 most_recent * 100 / trigger_thresh, most_recent, trigger_thresh);
	}

	if (most_recent >= trigger_thresh) {
		scnprintf(message, maxlen, "It's been %lu instructions since "
			  "the last preemption point; but the past average is "
			  "%lu -- I think you're stuck in an infinite loop.",
			  most_recent, average_triggers);
		return true;
	}

	/* FIXME: The one remaining case is an infinite loop around PPs during
	 * the 0th branch. Need a more conservative, dumber heuristic there. */
	if (ls->save.total_jumps == 0)
		return false;

	/* Have we been spinning around a choice point (so this branch would
	 * end up being infinitely deep)? Compute a "depth factor" that
	 * reflects our confidence in how accurate the past average branch
	 * depth was -- the fewer branches explored so far, the less so. */
	long double depth_factor = PROGRESS_DEPTH_FACTOR;
	/* For each branch short of the magic number, become less confident. */
	for (unsigned int i = ls->save.total_jumps;
	     i < PROGRESS_CONFIDENT_BRANCHES; i++) {
		depth_factor *= PROGRESS_BRANCH_UNCERTAINTY_EXPONENT;
	}
	unsigned int average_depth =
		ls->save.depth_total / (1 + ls->save.total_jumps);
	unsigned long depth_thresh = average_depth * depth_factor;
	/* we're right on top of the PP; if it's a DR PP, avoid emitting a bogus
	 * stack-address value as current eip (see dr eip logic in save.c). */
	bool during_dr_delay = ls->save.current->eip == ls->eip &&
	                       ls->save.current->data_race_eip != -1;
	if (ls->save.current->depth > depth_thresh && !during_dr_delay) {
		scnprintf(message, maxlen, "This interleaving has at least %d "
			  "preemption-points; but past branches on average were "
			  "only %d deep -- I think you're stuck in an infinite "
			  "loop.", ls->save.current->depth, average_depth);
		return true;
	}

	return false;
}

static bool ensure_progress(struct ls_state *ls)
{
	char *buf;
	unsigned int tid = ls->sched.cur_agent->tid;
	char message[BUF_SIZE];

	if (kern_panicked(ls->cpu0, ls->eip, &buf)) {
		if (testing_userspace()) {
			wrong_panic(ls, "kernel", "user");
		} else {
			FOUND_A_BUG(ls, "KERNEL PANIC: %s", buf);
		}
		MM_FREE(buf);
		return false;
	} else if (user_panicked(ls->cpu0, ls->eip, &buf) &&
		   !(TID_IS_INIT(tid) || TID_IS_SHELL(tid) || TID_IS_IDLE(tid))) {
		if (testing_userspace()) {
			FOUND_A_BUG(ls, "USERSPACE PANIC: %s", buf);
		} else {
			wrong_panic(ls, "user", "kernel");
		}
		MM_FREE(buf);
		return false;
	} else if (user_report_end_fail(ls->cpu0, ls->eip)) {
		FOUND_A_BUG(ls, "User test program reported failure!");
		return false;
	} else if (kern_page_fault_handler_entering(ls->eip)) {
		unsigned int from_eip = READ_STACK(ls->cpu0, 1);
		unsigned int cr2 = GET_CPU_ATTR(ls->cpu0, cr2);
		lsprintf(DEV, "page fault on address 0x%x from ", cr2);
		print_eip(DEV, from_eip);
		printf(DEV, "\n");
		/* did it come from kernel-space? */
		if (KERNEL_MEMORY(from_eip)) {
			if (!testing_userspace()) {
				lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW "Note: "
					 "If kernel page faults are expected "
					 "behaviour, unset PAGE_FAULT_WRAPPER "
					 "in config.landslide.\n");
			}
			FOUND_A_BUG(ls, "Kernel page faulted! Faulting eip: "
				    "0x%x; address: 0x%x", from_eip, cr2);
			return false;
		} else {
			ls->sched.cur_agent->last_pf_eip = from_eip;
			ls->sched.cur_agent->last_pf_cr2 = cr2;
			return true;
		}
	} else if (kern_killed_faulting_user_thread(ls->cpu0, ls->eip)) {
#ifdef PINTOS_KERNEL
		if (true) {
#else
		if (testing_userspace()) {
#endif
			int exn_num = ls->sched.cur_agent->most_recent_syscall;
			if (exn_num == 0) {
				unsigned int pf_eip =
					ls->sched.cur_agent->last_pf_eip;
				if (pf_eip == -1) {
					lsprintf(DEV, COLOUR_BOLD COLOUR_YELLOW
						 "Warning: MRS = 0 during "
						 "CAUSE_FAULT. Probably bug or "
						 "annotation error?\n");
					FOUND_A_BUG(ls, "TID %d was killed!\n",
						    ls->sched.cur_agent->tid);
				} else {
					unsigned int pf_cr2 =
						ls->sched.cur_agent->last_pf_cr2;
					FOUND_A_BUG(ls, "TID %d was killed by "
						    "a page fault! (Faulting "
						    "eip: 0x%x; addr: 0x%x)\n",
						    ls->sched.cur_agent->tid,
						    pf_eip, pf_cr2);
				}
			} if (exn_num >= ARRAY_SIZE(exception_names)) {
				FOUND_A_BUG(ls, "TID %d was killed by a fault! "
					    "(unknown exception #%u)\n",
					    ls->sched.cur_agent->tid, exn_num);
			} else {
				FOUND_A_BUG(ls, "TID %d was killed by a fault! "
					    "(exception #%u: %s)\n",
					    ls->sched.cur_agent->tid, exn_num,
					    exception_names[exn_num]);
			}
			return false;
		} else {
			lsprintf(DEV, "Kernel kills faulting thread %d\n",
				 ls->sched.cur_agent->tid);
			return true;
		}
	} else if (check_infinite_loop(ls, message, BUF_SIZE)) {
		const char *headline = "NO PROGRESS (infinite loop?)";
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "%s\n", message);
		FOUND_A_BUG_HTML_INFO(ls, headline, strlen(headline), html_env,
			HTML_PRINTF(html_env, "%s" HTML_NEWLINE, message);
			if (IN_USER_SYNC_PRIMITIVES(ls->sched.cur_agent)) {
				HTML_PRINTF(html_env, HTML_NEWLINE HTML_BOX_BEGIN);
				HTML_PRINTF(html_env, "<b>NOTE: I have run a loop "
					    "in %s() an alarming number of times."
					    HTML_NEWLINE,
					    USER_SYNC_ACTION_STR(ls->sched.cur_agent));
				HTML_PRINTF(html_env, "This version of "
					    "Landslide cannot distinguish "
					    "between this loop " HTML_NEWLINE);
				HTML_PRINTF(html_env, "being infinite versus "
					    "merely undesirable." HTML_NEWLINE);
				HTML_PRINTF(html_env, "Please refer to the "
					    "\"Synchronization (2)\" lecture."
					    HTML_NEWLINE);
				HTML_PRINTF(html_env, HTML_BOX_END HTML_NEWLINE);
			}
		);
		return false;
#ifdef PINTOS_KERNEL
	} else if (ls->eip == GUEST_PRINTF) {
		unsigned int esp = GET_CPU_ATTR(ls->cpu0, esp);
		char *fmt = read_string(
			ls->cpu0, READ_MEMORY(ls->cpu0, esp + WORD_SIZE));
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_BLUE "klog: %s", fmt);
		if (fmt[strlen(fmt)-1] != '\n') {
			printf(ALWAYS, "\n");
		}
		if (strstr(fmt, "%") != NULL && strstr(fmt, "%")[1] == 's') {
			MM_FREE(fmt);
			fmt = read_string(
				ls->cpu0, READ_MEMORY(ls->cpu0, esp + (2*WORD_SIZE)));
			lsprintf(ALWAYS, COLOUR_BOLD COLOUR_BLUE
				 "klog fmt string: %s\n", fmt);
		}
		MM_FREE(fmt);
		return true;
	} else if (ls->eip == GUEST_DBG_PANIC) {
		unsigned int esp = GET_CPU_ATTR(ls->cpu0, esp);
		char *fmt = read_string(
			ls->cpu0, READ_MEMORY(ls->cpu0, esp + (4*WORD_SIZE)));
		lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "panic: %s", fmt);
		if (fmt[strlen(fmt)-1] != '\n') {
			printf(ALWAYS, "\n");
		}
		MM_FREE(fmt);
		return true;
#endif
	} else {
		return true;
	}
}

static bool test_ended_safely(struct ls_state *ls)
{
	/* Anything that would indicate failure - e.g. return code... */

	// XXX: Check is broken. Can't reenable it meaningfully
	// XXX: (at least for pintos). See #181.
	// TODO: Test on pebbles; would "ifndef PINTOS_KERNEL" instead be ok?
#if 0
	// TODO: find the blocks that were leaked and print stack traces for them
	// TODO: the test could copy the heap to indicate which blocks
	// TODO: do some assert analogous to wrong_panic() for this
	if (ls->test.start_kern_heap_size < ls->kern_mem.heap_size) {
		FOUND_A_BUG(ls, "KERNEL MEMORY LEAK (%d bytes)!",
			    ls->kern_mem.heap_size - ls->test.start_kern_heap_size);
		return false;
	} else if (ls->test.start_user_heap_size < ls->user_mem.heap_size) {
		FOUND_A_BUG(ls, "USER MEMORY LEAK (%d bytes)!",
			    ls->user_mem.heap_size - ls->test.start_user_heap_size);
		return false;
	}
#endif

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
	if (test_update_state(ls) && !ls->test.test_is_running) {
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
	memcpy(ls->instruction_text, entry->value.text, 16);

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

		/* NB. mem update must come first because sched update contains
		 * the logic to create PPs, and snapshots must include state
		 * machine changes from mem update (tracking malloc/free). */
		mem_update(ls);
		sched_update(ls);
		check_test_state(ls);
	}
}
