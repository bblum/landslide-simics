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
	hax_t *empty = MM_ZALLOC(1, hax_t);
	SIM_log_constructor(&empty->log, parse_obj);
	empty->hax_count = 0;
	return &empty->log.obj;
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
	HAX_ATTR_REGISTER(conf_class, cpu0, "o", "The system's cpu0");
	HAX_ATTR_REGISTER(conf_class, phys_mem0, "o", "The system's phys_mem0");

	printf("welcome to hax.\n");
}

/******************************************************************************
 * actual interesting hax logic
 ******************************************************************************/

/* Main entry point. Called every instruction, data access, and exception. */
static void hax_consume(conf_object_t *obj, trace_entry_t *entry)
{
	hax_t *h = (hax_t *)obj;

	assert(h->hax_magic == HAX_MAGIC && "The glue didn't do its job!");

	h->hax_count++;
	if (h->hax_count % 1000000 == 0) {
		printf("hax number %d with trace-type %s\n", h->hax_count,
		       entry->trace_type == TR_Data ? "DATA" :
		       entry->trace_type == TR_Instruction ? "INSTR" : "EXN");
	}
}
