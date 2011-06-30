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

#include "landslide.h"
#include "schedule.h"

/******************************************************************************
 * simics glue
 ******************************************************************************/

static conf_object_t *ls_new_instance(parse_object_t *parse_obj)
{
	ls_state_t *ls = MM_ZALLOC(1, ls_state_t);
	SIM_log_constructor(&ls->log, parse_obj);
	ls->trigger_count = 0;

	ls->cpu0 = SIM_get_object("cpu0");
	assert(ls->cpu0 && "failed to find cpu");
	ls->kbd0 = SIM_get_object("kbd0");
	assert(ls->kbd0 && "failed to find keyboard");

#ifdef CAUSE_TIMER_LOLOL
	sp_init(&ls->active_threads);
#endif

	return &ls->log.obj;
}

/* type should be one of "integer", "boolean", "object", ... */
#define LS_ATTR_SET_GET_FNS(name, type)				\
	static set_error_t set_ls_##name##_attribute(			\
		void *arg, conf_object_t *obj, attr_value_t *val,	\
		attr_value_t *idx)					\
	{								\
		((ls_state_t *)obj)->name = SIM_attr_##type(*val);	\
		return Sim_Set_Ok;					\
	}								\
	static attr_value_t get_ls_##name##_attribute(			\
		void *arg, conf_object_t *obj, attr_value_t *idx)	\
	{								\
		return SIM_make_attr_##type(((ls_state_t *)obj)->name);	\
	}

/* type should be one of "\"i\"", "\"b\"", "\"o\"", ... */
#define LS_ATTR_REGISTER(class, name, type, desc)			\
	SIM_register_typed_attribute(class, #name,			\
				     get_ls_##name##_attribute, NULL,	\
				     set_ls_##name##_attribute, NULL,	\
				     Sim_Attr_Optional, type, NULL,	\
				     desc);

LS_ATTR_SET_GET_FNS(trigger_count, integer);

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
	conf_class_t *conf_class = SIM_register_class(MODULE_NAME, &funcs);

	/* Register the landslide class as a trace consumer. */
	static const trace_consume_interface_t sploits = {
		.consume = ls_consume
	};
	SIM_register_interface(conf_class, TRACE_CONSUME_INTERFACE, &sploits);

	/* Register attributes for the class. */
	LS_ATTR_REGISTER(conf_class, trigger_count, "i", "Count of haxes");

	printf("welcome to landslide.\n");
}

/******************************************************************************
 * miscellaneous other support code
 ******************************************************************************/

#define GET_CPU_ATTR(cpu, name) SIM_attr_integer(SIM_get_attribute(cpu, #name))
#define SET_CPU_ATTR(cpu, name, val) do {				\
		attr_value_t noob = SIM_make_attr_integer(val);		\
		set_error_t ret = SIM_set_attribute(cpu, #name, &noob);	\
		assert(ret == Sim_Set_Ok && "SET_CPU_ATTR failed!");	\
	} while (0)

/* two possible methods for causing a timer interrupt - the lolol version crafts
 * an iret stack frame by hand and changes the cpu's registers manually; the
 * other way just manipulates the cpu's interrupt pending flags to make it do
 * the interrupt itself. */
static void cause_timer_interrupt(ls_state_t *ls)
{
// #define CAUSE_TIMER_LOLOL
#ifdef CAUSE_TIMER_LOLOL
# define TIMER_HANDLER_WRAPPER 0x001035bc // TODO: reduce discosity
# define KERNEL_SEGSEL_CS 0x10

	int esp = GET_CPU_ATTR(ls->cpu0, esp);
	int eip = GET_CPU_ATTR(ls->cpu0, eip);
	int eflags = GET_CPU_ATTR(ls->cpu0, eflags);

	/* 12 is the size of an IRET frame only when already in kernel mode. */
	SET_CPU_ATTR(ls->cpu0, esp, esp - 12);
	esp = esp - 12; /* "oh, I can do common subexpression elimination!" */
	SIM_write_phys_memory(ls->cpu0, esp + 8, eflags, 4);
	SIM_write_phys_memory(ls->cpu0, esp + 4, KERNEL_SEGSEL_CS, 4);
	SIM_write_phys_memory(ls->cpu0, esp + 0, eip, 4);
	SET_CPU_ATTR(ls->cpu0, eip, TIMER_HANDLER_WRAPPER);

#else
# define TIMER_INTERRUPT_NUMBER 0x20

	if (GET_CPU_ATTR(ls->cpu0, pending_vector_valid)) {
		SET_CPU_ATTR(ls->cpu0, pending_vector,
			     GET_CPU_ATTR(ls->cpu0, pending_vector)
			     | TIMER_INTERRUPT_NUMBER);
	} else {
		SET_CPU_ATTR(ls->cpu0, pending_vector, TIMER_INTERRUPT_NUMBER);
		SET_CPU_ATTR(ls->cpu0, pending_vector_valid, 1);
	}

	SET_CPU_ATTR(ls->cpu0, pending_interrupt, 1);

#endif
}

/* keycodes for the keyboard buffer */
static int i8042_key(char c)
{
	static const int i8042_keys[] = {
		['0'] = 18, ['1'] = 19, ['2'] = 20, ['3'] = 21, ['4'] = 22,
		['5'] = 23, ['6'] = 24, ['7'] = 25, ['8'] = 26, ['9'] = 27,
		['a'] = 28, ['b'] = 29, ['c'] = 30, ['d'] = 31, ['e'] = 32,
		['f'] = 33, ['g'] = 34, ['h'] = 35, ['i'] = 36, ['j'] = 37,
		['k'] = 38, ['l'] = 39, ['m'] = 40, ['n'] = 41, ['o'] = 42,
		['p'] = 43, ['q'] = 44, ['r'] = 45, ['s'] = 46, ['t'] = 47,
		['u'] = 48, ['v'] = 49, ['w'] = 50, ['x'] = 51, ['y'] = 52,
		['z'] = 53, ['\''] = 54, [','] = 55, ['.'] = 56, [';'] = 57,
		['='] = 58, ['/'] = 59, ['\\'] = 60, [' '] = 61, ['['] = 62,
		[']'] = 63, ['-'] = 64, ['`'] = 65,             ['\n'] = 67,
	};
	assert(i8042_keys[(int)c] != 0 && "Attempt to type an unsupported key");
	return i8042_keys[(int)c];
}

static void cause_keypress(ls_state_t *ls, int key)
{
	attr_value_t i = SIM_make_attr_integer(key);
	attr_value_t v = SIM_make_attr_integer(0); /* see i8042 docs */

	set_error_t ret = SIM_set_attribute_idx(ls->kbd0, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress press failed!");

	v = SIM_make_attr_integer(1);
	ret = SIM_set_attribute_idx(ls->kbd0, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress release failed!");
}

/******************************************************************************
 * actual interesting landslide logic
 ******************************************************************************/

#define TEST_STRING "mandelbrot\n"
static void cause_test(ls_state_t *ls)
{
	int i;
	for (i = 0; i < strlen(TEST_STRING); i++) {
		cause_keypress(ls, i8042_key(TEST_STRING[i]));
	}
}

// TODO: delete these
#define FORK_AFTER_CHILD 0x103ee1 // pobbles
// #define FORK_AFTER_CHILD 0x1068f4 // pathos
// #define FORK_AFTER_CHILD 0x104d5e // bros

static void decide_whether_to_hax(ls_state_t *ls, trace_entry_t *entry, int eip)
{
	#ifdef CAUSE_TIMER_LOLOL
	int esp = GET_CPU_ATTR(ls->cpu0, esp);

	if (sp_remove(&ls->active_threads, esp)) {
		printf("returned to a thread we interrupted -- continuing;\n");
		return;
	}
#endif

	if (entry->trace_type == TR_Instruction && eip == FORK_AFTER_CHILD) {

		printf("in fork after child; invoking hax\n");
		// cause_test(ls);

#ifdef CAUSE_TIMER_LOLOL
		sp_add(&ls->active_threads, esp);
#endif
		cause_timer_interrupt(ls);
	}
}

/* Main entry point. Called every instruction, data access, and extensible. */
static void ls_consume(conf_object_t *obj, trace_entry_t *entry)
{
	ls_state_t *ls = (ls_state_t *)obj;

	ls->trigger_count++;

	int eip = GET_CPU_ATTR(ls->cpu0, eip);

	if (ls->trigger_count % 1000000 == 0) {
		printf("hax number %d with trace-type %s at 0x%x\n",
		       ls->trigger_count,
		       entry->trace_type == TR_Data ? "DATA" :
		       entry->trace_type == TR_Instruction ? "INSTR" : "EXN",
		       eip);
	}

	int old_thread = ls->current_thread;
	ls->current_thread = get_current_thread(ls);
	if (old_thread != ls->current_thread) {
		printf("switched threads %d -> %d at 0x%x\n", old_thread,
		       ls->current_thread, eip);
	}

	decide_whether_to_hax(ls, entry, eip);
}
