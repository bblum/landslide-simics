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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simics/api.h>
#include <simics/alloc.h>
#include <simics/utils.h>
#include <simics/arch/x86.h>

// XXX: idiots wrote this header, so it must be after the other includes.
#include "trace.h"

#define MODULE_NAME "LANDSLIDE"
#define MODULE_COLOUR COLOUR_DARK COLOUR_MAGENTA

#include "common.h"
#include "explore.h"
#include "found_a_bug.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "save.h"
#include "test.h"
#include "tree.h"
#include "x86.h"

/******************************************************************************
 * simics glue
 ******************************************************************************/

static conf_object_t *ls_new_instance(parse_object_t *parse_obj)
{
	struct ls_state *ls = MM_ZALLOC(1, struct ls_state);
	assert(ls && "failed to allocate ls state");
	SIM_log_constructor(&ls->log, parse_obj);
	ls->trigger_count = 0;
	ls->absolute_trigger_count = 0;

	ls->cpu0 = SIM_get_object("cpu0");
	assert(ls->cpu0 && "failed to find cpu");
	ls->kbd0 = SIM_get_object("kbd0");
	assert(ls->kbd0 && "failed to find keyboard");

	sched_init(&ls->sched);
	arbiter_init(&ls->arbiter);
	save_init(&ls->save);
	test_init(&ls->test);
	mem_init(&ls->mem);

	ls->cmd_file = NULL;
	ls->just_jumped = false;

	return &ls->log.obj;
}

/* type should be one of "integer", "boolean", "object", ... */
#define LS_ATTR_SET_GET_FNS(name, type)				\
	static set_error_t set_ls_##name##_attribute(			\
		void *arg, conf_object_t *obj, attr_value_t *val,	\
		attr_value_t *idx)					\
	{								\
		((struct ls_state *)obj)->name = SIM_attr_##type(*val);	\
		return Sim_Set_Ok;					\
	}								\
	static attr_value_t get_ls_##name##_attribute(			\
		void *arg, conf_object_t *obj, attr_value_t *idx)	\
	{								\
		return SIM_make_attr_##type(				\
			((struct ls_state *)obj)->name);		\
	}

/* type should be one of "\"i\"", "\"b\"", "\"o\"", ... */
#define LS_ATTR_REGISTER(class, name, type, desc)			\
	SIM_register_typed_attribute(class, #name,			\
				     get_ls_##name##_attribute, NULL,	\
				     set_ls_##name##_attribute, NULL,	\
				     Sim_Attr_Optional, type, NULL,	\
				     desc);

LS_ATTR_SET_GET_FNS(trigger_count, integer);

// XXX: figure out how to use simics list/string attributes
static set_error_t set_ls_arbiter_choice_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	int tid = SIM_attr_integer(*val);

	/* XXX: dum hack */
	if (tid == -42) {
		return Sim_Set_Not_Writable;
	} else {
		arbiter_append_choice(&((struct ls_state *)obj)->arbiter, tid);
		return Sim_Set_Ok;
	}
}
static attr_value_t get_ls_arbiter_choice_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	return SIM_make_attr_integer(-42);
}

static set_error_t set_ls_cmd_file_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	struct ls_state *ls = (struct ls_state *)obj;
	if (ls->cmd_file == NULL) {
		ls->cmd_file = MM_XSTRDUP(SIM_attr_string(*val));
		return Sim_Set_Ok;
	} else {
		return Sim_Set_Not_Writable;
	}
}
static attr_value_t get_ls_cmd_file_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	struct ls_state *ls = (struct ls_state *)obj;
	const char *path = ls->cmd_file != NULL ? ls->cmd_file : "/dev/null";
	return SIM_make_attr_string(path);
}

// save_path is deprecated. TODO remove
#if 0
static set_error_t set_ls_save_path_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	if (save_set_base_dir(&((struct ls_state *)obj)->save,
			      SIM_attr_string(*val))) {
		return Sim_Set_Ok;
	} else {
		return Sim_Set_Not_Writable;
	}
}
static attr_value_t get_ls_save_path_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	const char *path = save_get_path(&((struct ls_state *)obj)->save);
	return SIM_make_attr_string(path);
}
#endif

static set_error_t set_ls_test_case_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	if (cause_test(((struct ls_state *)obj)->kbd0,
		       &((struct ls_state *)obj)->test, (struct ls_state *)obj,
		       SIM_attr_string(*val))) {
		return Sim_Set_Ok;
	} else {
		return Sim_Set_Not_Writable;
	}
}
static attr_value_t get_ls_test_case_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	const char *path = ((struct ls_state *)obj)->test.current_test;
	return SIM_make_attr_string(path);
}


/* Forward declaration. */
static void ls_consume(conf_object_t *obj, trace_entry_t *entry);

/* init_local() is called once when the device module is loaded into Simics */
void init_local(void)
{
	const class_data_t funcs = {
		.new_instance = ls_new_instance,
		.class_desc = "hax and sploits",
		.description = "here we have a simix module which provides not"
			" only hax or sploits individually but rather a great"
			" conjunction of the two."
	};

	/* Register the empty device class. */
	conf_class_t *conf_class = SIM_register_class(SIM_MODULE_NAME, &funcs);

	/* Register the landslide class as a trace consumer. */
	static const trace_consume_interface_t sploits = {
		.consume = ls_consume
	};
	SIM_register_interface(conf_class, TRACE_CONSUME_INTERFACE, &sploits);

	/* Register attributes for the class. */
	LS_ATTR_REGISTER(conf_class, trigger_count, "i", "Count of haxes");
	LS_ATTR_REGISTER(conf_class, arbiter_choice, "i",
			 "Tell the arbiter which thread to choose next "
			 "(buffered, FIFO)");
#if 0
	LS_ATTR_REGISTER(conf_class, save_path, "s",
			 "Base directory of saved choices for this test case");
#endif
	LS_ATTR_REGISTER(conf_class, test_case, "s",
			 "Which test case should we run?");
	LS_ATTR_REGISTER(conf_class, cmd_file, "s",
			 "Filename to use for communication with the wrapper");

	lsprintf(ALWAYS, "welcome to landslide.\n");
}

/******************************************************************************
 * Other simics goo
 ******************************************************************************/

// TODO: move the command running from save.c over to here

#define SYMTABLE_NAME "deflsym"

#define LIKELY_DIR "pebsim/"

#define FUNCTION_COLOUR      COLOUR_BOLD COLOUR_CYAN
#define FUNCTION_INFO_COLOUR COLOUR_DARK COLOUR_GREY
#define GLOBAL_COLOUR        COLOUR_BOLD COLOUR_MAGENTA
#define GLOBAL_INFO_COLOUR   COLOUR_DARK COLOUR_GREY

int symtable_lookup(char *buf, int maxlen, int addr)
{
	// TODO: store deflsym in struct ls_state
	conf_object_t *table = SIM_get_object(SYMTABLE_NAME);
	if (table == NULL) {
		return snprintf(buf, maxlen, "<no symtable>");
	}

	attr_value_t idx = SIM_make_attr_integer(addr);
	attr_value_t result = SIM_get_attribute_idx(table, "source_at", &idx);
	if (!SIM_attr_is_list(result)) {
		return snprintf(buf, maxlen, "<unknown>");
	}
	assert(SIM_attr_list_size(result) >= 3);

	const char *file = SIM_attr_string(SIM_attr_list_item(result, 0));
	int line = SIM_attr_integer(SIM_attr_list_item(result, 1));
	const char *func = SIM_attr_string(SIM_attr_list_item(result, 2));

	/* A hack to make the filenames shorter */
	if (strstr(file, LIKELY_DIR) != NULL) {
		file = strstr(file, LIKELY_DIR) + strlen(LIKELY_DIR);
	}

	int ret = snprintf(buf, maxlen, FUNCTION_COLOUR "%s"
			   FUNCTION_INFO_COLOUR " (%s:%d)" COLOUR_DEFAULT,
			   func, file, line);

	SIM_free_attribute(result);
	return ret;
}

int symtable_lookup_data(char *buf, int maxlen, int addr)
{
	// TODO: store deflsym in struct ls_state
	conf_object_t *table = SIM_get_object(SYMTABLE_NAME);
	if (table == NULL) {
		return snprintf(buf, maxlen, GLOBAL_COLOUR "global0x%.8x"
				COLOUR_DEFAULT, addr);
	}

	attr_value_t idx = SIM_make_attr_integer(addr);
	attr_value_t result = SIM_get_attribute_idx(table, "data_at", &idx);
	if (!SIM_attr_is_list(result)) {
		return snprintf(buf, maxlen, GLOBAL_COLOUR "global0x%.8x"
				COLOUR_DEFAULT, addr);
	}
	assert(SIM_attr_list_size(result) >= 4);

	const char *globalname = SIM_attr_string(SIM_attr_list_item(result, 1));
	const char *typename = SIM_attr_string(SIM_attr_list_item(result, 2));
	int offset = SIM_attr_integer(SIM_attr_list_item(result, 3));

	int ret = snprintf(buf, maxlen, GLOBAL_COLOUR "%s", globalname);
	if (offset != 0) {
		ret += snprintf(buf+ret, maxlen-ret, "+%d", offset);
	}
	ret += snprintf(buf+ret, maxlen-ret, GLOBAL_INFO_COLOUR
			" (%s at 0x%.8x)" COLOUR_DEFAULT, typename, addr);

	SIM_free_attribute(result);
	return ret;
}

bool function_eip_offset(int eip, int *offset)
{
	conf_object_t *table = SIM_get_object(SYMTABLE_NAME);
	if (table == NULL) {
		return false;
	}

	attr_value_t idx = SIM_make_attr_integer(eip);
	attr_value_t result = SIM_get_attribute_idx(table, "source_at", &idx);
	if (!SIM_attr_is_list(result)) {
		return false;
	}
	assert(SIM_attr_list_size(result) >= 3);

	attr_value_t name = SIM_attr_list_item(result, 2);
	attr_value_t func = SIM_get_attribute_idx(table, "symbol_value", &name);

	*offset = eip - SIM_attr_integer(func);
	return true;
}

/******************************************************************************
 * pebbles system calls
 ******************************************************************************/

#define FORK_INT            0x41
#define EXEC_INT            0x42
/* #define EXIT_INT            0x43 */
#define WAIT_INT            0x44
#define YIELD_INT           0x45
#define DESCHEDULE_INT      0x46
#define MAKE_RUNNABLE_INT   0x47
#define GETTID_INT          0x48
#define NEW_PAGES_INT       0x49
#define REMOVE_PAGES_INT    0x4A
#define SLEEP_INT           0x4B
#define GETCHAR_INT         0x4C
#define READLINE_INT        0x4D
#define PRINT_INT           0x4E
#define SET_TERM_COLOR_INT  0x4F
#define SET_CURSOR_POS_INT  0x50
#define GET_CURSOR_POS_INT  0x51
#define THREAD_FORK_INT     0x52
#define GET_TICKS_INT       0x53
#define MISBEHAVE_INT       0x54
#define HALT_INT            0x55
#define LS_INT              0x56
#define TASK_VANISH_INT     0x57 /* previously known as TASK_EXIT_INT */
#define SET_STATUS_INT      0x59
#define VANISH_INT          0x60
/* #define CAS2I_RUNFLAG_INT   0x61 */
#define SWEXN_INT           0x74

#define CASE_SYSCALL(num, name) \
	case (num): printf(CHOICE, "%s()\n", (name)); break

static void check_user_syscall(struct ls_state *ls)
{
	if (READ_BYTE(ls->cpu0, ls->eip) != OPCODE_INT)
		return;

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
		CASE_SYSCALL(SWEXN_INT, "swexn");
		default:
			printf(CHOICE, "((unknown 0x%x))\n", number);
			break;
	}
}
#undef CASE_SYSCALL

/******************************************************************************
 * actual interesting landslide logic
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

static bool ensure_progress(struct ls_state *ls)
{
	char *buf;
	if (kern_panicked(ls->cpu0, ls->eip, &buf)) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED "KERNEL PANIC: %s\n", buf);
		MM_FREE(buf);
		return false;
	}

	/* FIXME: find a less false-negative way to tell when we have enough
	 * data */
	STATIC_ASSERT(PROGRESS_MIN_BRANCHES > 0); /* prevent div-by-zero */
	if (ls->save.total_jumps < PROGRESS_MIN_BRANCHES)
		return true;

	/* Have we been spinning since the last choice point? */
	int most_recent = ls->trigger_count - ls->save.current->trigger_count;
	int average_triggers = ls->save.total_triggers / ls->save.total_choices;
	if (most_recent > average_triggers * PROGRESS_TRIGGER_FACTOR) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "NO PROGRESS (infinite loop?)\n");
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "%d instructions since last decision; average %d\n",
			 most_recent, average_triggers);
		return false;
	}

	/* Have we been spinning around a choice point (so this branch would
	 * end up being infinitely deep)? */
	int average_depth = ls->save.depth_total / (1 + ls->save.total_jumps);
	if (ls->save.current->depth > average_depth * PROGRESS_DEPTH_FACTOR) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "NO PROGRESS (stuck thread(s)?)\n");
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "Current branch depth %d; average depth %d\n",
			 ls->save.current->depth, average_depth);
		return false;
	}

	return true;
}

static bool test_ended_safely(struct ls_state *ls)
{
	/* Anything that would indicate failure - e.g. return code... */

	// TODO: find the blocks that were leaked and print stack traces for them
	if (ls->test.start_heap_size > ls->mem.heap_size) {
		// TODO: the test could copy the heap to indicate which blocks
		lsprintf(BUG, COLOUR_BOLD COLOUR_RED
			 "MEMORY LEAK (%d bytes)!\n",
			 ls->test.start_heap_size - ls->mem.heap_size);
		return false;
	}

	return true;
}

static bool time_travel(struct ls_state *ls)
{
	int tid;
	struct hax *h;

	/* find where we want to go in the tree, and choose what to do there */
	if ((h = explore(ls->save.root, ls->save.current, &tid)) != NULL) {
		assert(!h->all_explored);
		arbiter_append_choice(&ls->arbiter, tid);
		save_longjmp(&ls->save, ls, h);
		return true;
	} else {
		return false;
	}
}

static void found_no_bug(struct ls_state *ls)
{
	lsprintf(ALWAYS, "**** Execution tree explored; you survived! ****\n");
	PRINT_TREE_INFO(ALWAYS, ls);
	SIM_quit(LS_NO_KNOWN_BUG);
}

static void check_test_state(struct ls_state *ls)
{
	/* When a test case finishes, break the simulation so the wrapper can
	 * decide what to do. */
	if (test_update_state(ls->cpu0, &ls->test, &ls->sched) &&
	    !ls->test.test_is_running) {
		/* See if it's time to try again... */
		if (ls->test.test_ever_caused) {
			lsprintf(BRANCH, "test case ended!\n");

			if (DECISION_INFO_ONLY != 0) {
				lsprintf(ALWAYS, COLOUR_BOLD COLOUR_GREEN
					 "These were the decision points:\n");
				dump_decision_info(ls);
			} else if (test_ended_safely(ls)) {
				save_setjmp(&ls->save, ls, -1, true, true,
					    false);
				if (!time_travel(ls)) {
					found_no_bug(ls);
				}
			} else {
				found_a_bug(ls);
			}
		} else {
			lsprintf(ALWAYS, "ready to roll!\n");
			SIM_break_simulation(NULL);
		}
	} else if (!ensure_progress(ls)) {
		found_a_bug(ls);
	}
}

/* Main entry point. Called every instruction, data access, and extensible. */
static void ls_consume(conf_object_t *obj, trace_entry_t *entry)
{
	struct ls_state *ls = (struct ls_state *)obj;

	ls->eip = GET_CPU_ATTR(ls->cpu0, eip);

	if (ls->eip >= USER_MEM_START) {
		if (entry->trace_type == TR_Instruction) {
			check_user_syscall(ls);
			check_test_state(ls);
		}
		return;
	} else if (entry->trace_type == TR_Data && entry->pa < USER_MEM_START) {
		mem_check_shared_access(ls, &ls->mem, entry->pa,
					(entry->read_or_write == Sim_RW_Write));
		return;
	} else if (entry->trace_type != TR_Instruction) {
		return;
	}

	ls->trigger_count++;
	ls->absolute_trigger_count++;

	if (ls->trigger_count % 1000000 == 0) {
		lsprintf(INFO, "hax number %lu (%lu) at 0x%x\n",
			 ls->trigger_count, ls->absolute_trigger_count,
			 ls->eip);
	}

	if (ls->just_jumped) {
		sched_recover(ls);
		ls->just_jumped = false;
	}

	sched_update(ls);
	mem_update(ls);
	check_test_state(ls);
}
