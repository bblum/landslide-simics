/**
 * @file simics_glue.c
 * @brief glue code to register and initialize as a simics module
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <simics/api.h>
#include <simics/utils.h>
#include <simics/arch/x86.h>

#include "trace.h"

#define MODULE_NAME "SIMICS_GLUE"

#include "landslide.h"
#include "found_a_bug.h"

#define SIM_MODULE_NAME "landslide"

static conf_object_t *ls_new_instance(parse_object_t *parse_obj)
{
	struct ls_state *ls = new_landslide();
	SIM_log_constructor(&ls->log, parse_obj);
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

static set_error_t set_ls_decision_trace_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	int value = SIM_attr_integer(*val);
	if (value != 0x15410de0u) {
		if (value == 0) {
			DUMP_DECISION_INFO_QUIET((struct ls_state *)obj);
		} else {
			DUMP_DECISION_INFO((struct ls_state *)obj);
		}
	}
	return Sim_Set_Ok;
}
static attr_value_t get_ls_decision_trace_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	return SIM_make_attr_integer(0x15410de0u);
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

static set_error_t set_ls_html_file_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	struct ls_state *ls = (struct ls_state *)obj;
	if (ls->html_file == NULL) {
		ls->html_file = MM_XSTRDUP(SIM_attr_string(*val));
		return Sim_Set_Ok;
	} else {
		return Sim_Set_Not_Writable;
	}
}
static attr_value_t get_ls_html_file_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	struct ls_state *ls = (struct ls_state *)obj;
	const char *path = ls->html_file != NULL ? ls->html_file : "/dev/null";
	return SIM_make_attr_string(path);
}

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

static set_error_t set_ls_quicksand_pps_attribute(
	void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
	struct ls_state *ls = (struct ls_state *)obj;
	if (load_dynamic_pps(ls, SIM_attr_string(*val))) {
		return Sim_Set_Ok;
	} else {
		return Sim_Set_Not_Writable;
	}
}
static attr_value_t get_ls_quicksand_pps_attribute(
	void *arg, conf_object_t *obj, attr_value_t *idx)
{
	return SIM_make_attr_string("/dev/null");
}

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
		/* argh, impossible to forward-declare trace_entry struct in header */
		.consume = (void (*)(conf_object_t *, trace_entry_t *))landslide_entrypoint
	};
	SIM_register_interface(conf_class, TRACE_CONSUME_INTERFACE, &sploits);

	/* Register attributes for the class. */
	LS_ATTR_REGISTER(conf_class, decision_trace, "i", "Get a decision trace.");
	LS_ATTR_REGISTER(conf_class, trigger_count, "i", "Count of haxes");
	LS_ATTR_REGISTER(conf_class, test_case, "s",
			 "Which test case should we run?");
	LS_ATTR_REGISTER(conf_class, cmd_file, "s",
			 "Filename to use for communication with the wrapper");
	LS_ATTR_REGISTER(conf_class, html_file, "s",
			 "Filename to use for HTML preemption trace output");
	LS_ATTR_REGISTER(conf_class, quicksand_pps, "s",
			 "Filename for dynamic Quicksand-supplied PP config");
}
