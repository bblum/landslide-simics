/*
  empty-device.c - Skeleton code to base new device modules on

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

/* USER-TODO: Set the name of the device class */
#define DEVICE_NAME "hax"

typedef struct {
        /* log_object_t must be the first thing in the device struct */
        log_object_t log;

        /* USER-TODO: Add user specific members here. The 'value' member
           is only an example to show how to implement an attribute */
        int value;
} hax_t;

/*
 * The new_instance() function is registered with the SIM_register_class
 * call (see init_local() below), and is used as a constructor for every
 * instance of the hax class.
 */
static conf_object_t *
new_instance(parse_object_t *parse_obj)
{
        hax_t *empty = MM_ZALLOC(1, hax_t);
        SIM_log_constructor(&empty->log, parse_obj);

        /* USER-TODO: Add initialization code for new instances here */
        empty->value = 31337;

        return &empty->log.obj;
}

static exception_type_t
operation(conf_object_t *obj, generic_transaction_t *mop, map_info_t info)
{
        hax_t *empty = (hax_t *)obj;
        int offset = (int)(SIM_get_mem_op_physical_address(mop)
                           + info.start - info.base);

        /* USER-TODO: Handle accesses to the device here */

        if (SIM_mem_op_is_read(mop)) {
                SIM_log_info(2, &empty->log, 0, "read from offset %d", offset);
                SIM_set_mem_op_value_le(mop, 0);
        } else {
                SIM_log_info(2, &empty->log, 0, "write to offset %d", offset);
        }
        return Sim_PE_No_Exception;
}

static set_error_t
set_value_attribute(void *arg, conf_object_t *obj,
                    attr_value_t *val, attr_value_t *idx)
{
        hax_t *empty = (hax_t *)obj;
        empty->value = SIM_attr_integer(*val);
        return Sim_Set_Ok;
}

static attr_value_t
get_value_attribute(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        hax_t *empty = (hax_t *)obj;
        return SIM_make_attr_integer(empty->value);
}

/* init_local() is called once when the device module is loaded into Simics */
void
init_local(void)
{
        printf("hax init called\n");

        const class_data_t funcs = {
                .new_instance = new_instance,
                .class_desc = "C device template",
                .description =
                "This is a descriptive description describing the "
                DEVICE_NAME " class descriptively."
        };

        /* Register the empty device class. */
        conf_class_t *conf_class = SIM_register_class(DEVICE_NAME, &funcs);

        /* Register the 'io_memory' interface, that is used to implement
           memory mapped accesses */
        static const io_memory_interface_t memory_iface = {
                .operation = operation
        };
        SIM_register_interface(conf_class, IO_MEMORY_INTERFACE, &memory_iface);

        /* USER-TODO: Add any attributes for the device here */

        SIM_register_typed_attribute(
                conf_class, "value",
                get_value_attribute, NULL,
                set_value_attribute, NULL,
                Sim_Attr_Optional,
                "i", NULL,
                "Value containing a valid valuable valuation.");
}
