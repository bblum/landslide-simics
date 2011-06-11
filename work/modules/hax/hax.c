/*
  hax.c - A Module for Simics which provides yon Hax and Sploits

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

// #include "util.h"

// XXX: idiots wrote this header, so it must be after the other includes.
#include "trace.h"

#define MODULE_NAME "hax"

typedef struct {
	/* log_object_t must be the first thing in the device struct */
	log_object_t log;

	int hax_count;
	int hax_magic;

	/* Pointers to the CPU and physical memory. Currently only supports
	 * one CPU and one memory space. The python glue must set these to
	 * cpu0 and phys_mem0. */
	conf_object_t *cpu0;
	conf_object_t *phys_mem0;
	// TODO: the above ones should be initialised using SIM_get_object().

	conf_object_t *kbd0;
} hax_t;

/* Whoever calls the constructor must set hax_magic to HAX_MAGIC as a promise
 * that it is also setting cpu0 and phys_mem0 correctly. */
#define HAX_MAGIC 0x15410FA1L

/******************************************************************************
 * simics glue
 ******************************************************************************/

/* initialise a new hax instance */
static conf_object_t *hax_new_instance(parse_object_t *parse_obj)
{
	hax_t *h = MM_ZALLOC(1, hax_t);
	SIM_log_constructor(&h->log, parse_obj);
	h->hax_count = 0;

	h->kbd0 = SIM_get_object("kbd0");
	assert(h->kbd0 && "failed to find keyboard");

	return &h->log.obj;
}

/* type should be one of "integer", "boolean", "object", ... */
#define HAX_ATTR_SET_GET_FNS(name, type)				\
	static set_error_t set_hax_##name##_attribute(			\
		void *arg, conf_object_t *obj, attr_value_t *val,	\
		attr_value_t *idx)					\
	{								\
		((hax_t *)obj)->name = SIM_attr_##type(*val);		\
		return Sim_Set_Ok;					\
	}								\
	static attr_value_t get_hax_##name##_attribute(			\
		void *arg, conf_object_t *obj, attr_value_t *idx)	\
	{								\
		return SIM_make_attr_##type(((hax_t *)obj)->name);	\
	}

/* type should be one of "\"i\"", "\"b\"", "\"o\"", ... */
#define HAX_ATTR_REGISTER(class, name, type, desc)			\
	SIM_register_typed_attribute(class, #name,			\
				     get_hax_##name##_attribute, NULL,	\
				     set_hax_##name##_attribute, NULL,	\
				     Sim_Attr_Required, type, NULL,	\
				     desc);

HAX_ATTR_SET_GET_FNS(hax_count, integer);
HAX_ATTR_SET_GET_FNS(hax_magic, integer);
HAX_ATTR_SET_GET_FNS(cpu0, object);
HAX_ATTR_SET_GET_FNS(phys_mem0, object);

/* Forward declaration. */
static void hax_consume(conf_object_t *obj, trace_entry_t *entry);

/* init_local() is called once when the device module is loaded into Simics */
void init_local(void)
{
	const class_data_t funcs = {
		.new_instance = hax_new_instance,
		.class_desc = "hax and sploits",
		.description = "here we have a simix module which provides not"
			" only hax or sploits individually but rather a great"
			" conjunction of the two."
	};

	/* Register the empty device class. */
	conf_class_t *conf_class = SIM_register_class(MODULE_NAME, &funcs);

	/* Register the hax class as a trace consumer. */
	static const trace_consume_interface_t sploits = {
		.consume = hax_consume
	};
	SIM_register_interface(conf_class, TRACE_CONSUME_INTERFACE, &sploits);

	/* Register attributes for the class. */
	HAX_ATTR_REGISTER(conf_class, hax_count, "i", "Count of haxes");
	HAX_ATTR_REGISTER(conf_class, hax_magic, "i", "Magic hax number");
	// TODO: use SIM_get_object to initialise these
	HAX_ATTR_REGISTER(conf_class, cpu0, "o", "The system's cpu0");
	HAX_ATTR_REGISTER(conf_class, phys_mem0, "o", "The system's phys_mem0");

	printf("welcome to hax.\n");
}

/******************************************************************************
 * miscellaneous other support code
 ******************************************************************************/

#define GET_CPU_REG(cpu, reg) SIM_attr_integer(SIM_get_attribute(cpu, #reg))
#define SET_CPU_REG(cpu, reg, val) do {					\
		attr_value_t noob = SIM_make_attr_integer(val);		\
		set_error_t ret = SIM_set_attribute(cpu, #reg, &noob);	\
		assert(ret == Sim_Set_Ok && "SET_CPU_REG failed!");	\
	} while (0)

#define TIMER_HANDLER_WRAPPER 0x001035bc // TODO: reduce discosity

#define KERNEL_SEGSEL_CS 0x10

static void cause_timer_interrupt(hax_t *h)
{
	int esp = GET_CPU_REG(h->cpu0, esp);
	int eip = GET_CPU_REG(h->cpu0, eip);
	int eflags = GET_CPU_REG(h->cpu0, eflags);

	/* 12 is the size of an IRET frame only when already in kernel mode. */
	SET_CPU_REG(h->cpu0, esp, esp - 12);
	esp = esp - 12; /* "oh, I can do common subexpression elimination!" */
	SIM_write_phys_memory(h->cpu0, esp + 8, eflags, 4);
	SIM_write_phys_memory(h->cpu0, esp + 4, KERNEL_SEGSEL_CS, 4);
	SIM_write_phys_memory(h->cpu0, esp + 0, eip, 4);
	SET_CPU_REG(h->cpu0, eip, TIMER_HANDLER_WRAPPER);
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

static void cause_keypress(hax_t *h, int key)
{
	attr_value_t i = SIM_make_attr_integer(key);
	attr_value_t v = SIM_make_attr_integer(0); /* see i8042 docs */

	set_error_t ret = SIM_set_attribute_idx(h->kbd0, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress press failed!");

	v = SIM_make_attr_integer(1);
	ret = SIM_set_attribute_idx(h->kbd0, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress release failed!");
}

/******************************************************************************
 * actual interesting hax logic
 ******************************************************************************/

#define TEST_STRING "mandelbrot\n"
static void cause_test(hax_t *h)
{
	int i;
	for (i = 0; i < strlen(TEST_STRING); i++) {
		cause_keypress(h, i8042_key(TEST_STRING[i]));
	}
}

// TODO: delete these
#define FORK_AFTER_CHILD 0x103ee1 // pobbles
// #define FORK_AFTER_CHILD 0x1068f4 // pathos

/* Main entry point. Called every instruction, data access, and extensible. */
static void hax_consume(conf_object_t *obj, trace_entry_t *entry)
{
	hax_t *h = (hax_t *)obj;

	assert(h->hax_magic == HAX_MAGIC && "The glue didn't do its job!");
	h->hax_count++;

	int eip = GET_CPU_REG(h->cpu0, eip);

	if (h->hax_count % 1000000 == 0) {
		printf("hax number %d with trace-type %s at 0x%x\n", h->hax_count,
		       entry->trace_type == TR_Data ? "DATA" :
		       entry->trace_type == TR_Instruction ? "INSTR" : "EXN",
		       eip);
	}

	if (entry->trace_type == TR_Instruction && eip == FORK_AFTER_CHILD && h->hax_count < 30000000) {
		printf("in fork after child; invoking hax\n");
		cause_test(h);
		// cause_timer_interrupt(h);
	}
}
