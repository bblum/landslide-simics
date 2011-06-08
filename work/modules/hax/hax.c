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
} hax_t;

/*
 * The new_instance() function is registered with the SIM_register_class
 * call (see init_local() below), and is used as a constructor for every
 * instance of the hax class.
 */
static conf_object_t *hax_new_instance(parse_object_t *parse_obj)
{
	hax_t *empty = MM_ZALLOC(1, hax_t);
	SIM_log_constructor(&empty->log, parse_obj);

	empty->hax_count = 0;
	empty->hax_magic = 0x15410DE0U;

	return &empty->log.obj;
}

#if 0
static cycles_t hax_main(conf_object_t *obj, conf_object_t *space,
			 map_list_t *map, generic_transaction_t *mem_op)
{
	hax_t *h = (hax_t *)obj;
	int offset = (int)(SIM_get_mem_op_physical_address(mem_op));

	h->hax_count++;
	if (h->hax_count % 1000 == 0) {
		printf("hax number %d at offset %d\n", h->hax_count, offset);
	}
	/* USER-TODO: Handle accesses to the device here */
	if (SIM_mem_op_is_read(mop)) {
		SIM_log_info(2, &empty->log, 0, "read from offset %d", offset);
		SIM_set_mem_op_value_le(mop, 0);
	} else {
		SIM_log_info(2, &empty->log, 0, "write to offset %d", offset);
	}
	return 0;
}
#endif

static void hax_consume(conf_object_t *obj, trace_entry_t *entry)
{
	hax_t *h = (hax_t *)obj;

	h->hax_count++;
	if (h->hax_count % 1000000 == 0) {
		printf("hax number %d with trace-type %s\n", h->hax_count,
		       entry->trace_type == TR_Data ? "DATA" :
		       entry->trace_type == TR_Instruction ? "INSTR" : "EXN");
	}
}

static set_error_t set_hax_attribute(void *arg, conf_object_t *obj,
				     attr_value_t *val, attr_value_t *idx)
{
	hax_t *empty = (hax_t *)obj;
	empty->hax_count = SIM_attr_integer(*val);
	return Sim_Set_Ok;
}

static attr_value_t get_hax_attribute(void *arg, conf_object_t *obj,
				      attr_value_t *idx)
{
	hax_t *empty = (hax_t *)obj;
	return SIM_make_attr_integer(empty->hax_count);
}

/* init_local() is called once when the device module is loaded into Simics */
void init_local(void)
{
	printf("welcome to hax.\n");

	const class_data_t funcs = {
		.new_instance = hax_new_instance,
		.class_desc = "hax and sploits",
		.description = "here we have a simix module which provides not"
			" only hax or sploits individually but rather a great"
			" conjunction of the two."
	};

	/* Register the empty device class. */
	conf_class_t *conf_class = SIM_register_class(MODULE_NAME, &funcs);

#if 0
	/* Register the 'io_memory' interface, that is used to implement
	 * memory mapped accesses */
	static const snoop_memory_interface_t memory_iface = {
		.operate = hax_main
	};
	SIM_register_interface(conf_class, SNOOP_MEMORY_INTERFACE,
			       &memory_iface);

	//attach_to_memory(conf_class);
#endif

	static const trace_consume_interface_t sploits = {
		.consume = hax_consume
	};
	SIM_register_interface(conf_class, TRACE_CONSUME_INTERFACE, &sploits);

	/* USER-TODO: Add any attributes for the device here */

	SIM_register_typed_attribute(
		conf_class, "hax_count",
		get_hax_attribute, NULL,
		set_hax_attribute, NULL,
		Sim_Attr_Optional,
		"i", NULL,
		"Value containing a valid valuable valuation.");
}
