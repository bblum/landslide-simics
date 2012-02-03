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
		ls->cmd_file = MM_STRDUP(SIM_attr_string(*val));
		assert(ls->cmd_file != NULL && "failed strdup!");
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

	lsprintf("welcome to landslide.\n");
}

/******************************************************************************
 * Other simics goo
 ******************************************************************************/

// TODO: move the command running from save.c over to here

#define SYMTABLE_NAME "deflsym"

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

	return snprintf(buf, maxlen, "%s (%s:%d)", func, file, line);
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
 * actual interesting landslide logic
 ******************************************************************************/

static bool check_test_failure(struct ls_state *ls)
{
	/* Anything that would indicate failure - e.g. return code... */

	if (ls->test.start_heap_size > ls->mem.heap_size) {
		// TODO: the test could copy the heap to indicate which blocks
		lsprintf("MEMORY LEAK (%d bytes)!\n",
			 ls->test.start_heap_size - ls->mem.heap_size);
		return true;
	}

	return false;
}

static void time_travel(struct ls_state *ls)
{
	int tid;
	struct hax *h;

	/* find where we want to go in the tree, and choose what to do there */
	if ((h = explore(ls->save.root, ls->save.current, &tid)) != NULL) {
		assert(!h->all_explored);
		arbiter_append_choice(&ls->arbiter, tid);
		save_longjmp(&ls->save, ls, h);
	} else {
		lsprintf("choice tree explored; you passed!\n");
		SIM_quit(LS_NO_KNOWN_BUG);
	}
}

/* Main entry point. Called every instruction, data access, and extensible. */
static void ls_consume(conf_object_t *obj, trace_entry_t *entry)
{
	struct ls_state *ls = (struct ls_state *)obj;

	if (entry->trace_type == TR_Data && entry->pa < USER_MEM_START) {
		mem_check_shared_access(ls, &ls->mem, entry->pa,
					(entry->read_or_write == Sim_RW_Write));
		return;
	} else if (entry->trace_type != TR_Instruction) {
		return;
	}

	ls->trigger_count++;
	ls->absolute_trigger_count++;

	/* TODO: avoid using get_cpu_attr */
	ls->eip = GET_CPU_ATTR(ls->cpu0, eip);

	if (ls->trigger_count % 1000000 == 0) {
		lsprintf("hax number %d (%d) with trace-type %s at 0x%x\n",
			 ls->trigger_count, ls->absolute_trigger_count,
			 entry->trace_type == TR_Data ? "DATA" :
			 entry->trace_type == TR_Instruction ? "INSTR" : "EXN",
			 ls->eip);
	}

	if (ls->eip >= USER_MEM_START) {
		return;
	}

	if (ls->just_jumped) {
		sched_recover(ls);
		ls->just_jumped = false;
	}

	sched_update(ls);

	/* When a test case finishes, break the simulation so the wrapper can
	 * decide what to do. */
	if (test_update_state(ls->cpu0, &ls->test, &ls->sched) &&
	    !ls->test.test_is_running) {
		/* See if it's time to try again... */
		if (ls->test.test_ever_caused) {
			lsprintf("test case ended!\n");

			if (check_test_failure(ls)) {
				found_a_bug(ls);
			} else {
				save_setjmp(&ls->save, ls, -1, true, true);
				time_travel(ls);
			}
		} else {
			lsprintf("ready to roll!\n");
			SIM_break_simulation(NULL);
		}
	}
}
