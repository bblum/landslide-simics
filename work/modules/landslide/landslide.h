/**
 * @file landslide.h
 * @brief common landslide stuff
 * @author Ben Blum
 */

#ifndef __LS_LANDSLIDE_H
#define __LS_LANDSLIDE_H

#include <simics/api.h>

#include "schedule.h"

#ifdef CAUSE_TIMER_LOLOL
#include "sp_table.h"
#endif

#define MODULE_NAME "landslide"

struct ls_state {
	/* log_object_t must be the first thing in the device struct */
	log_object_t log;

	int trigger_count;

	/* Pointers to relevant objects. Currently only supports one CPU. */
	conf_object_t *cpu0;
	conf_object_t *kbd0;
	int eip;

	struct sched_state sched;

#ifdef CAUSE_TIMER_LOLOL
	struct sp_table active_threads;
#endif
};

#define GET_CPU_ATTR(cpu, name) SIM_attr_integer(SIM_get_attribute(cpu, #name))
#define SET_CPU_ATTR(cpu, name, val) do {				\
		attr_value_t noob = SIM_make_attr_integer(val);		\
		set_error_t ret = SIM_set_attribute(cpu, #name, &noob);	\
		assert(ret == Sim_Set_Ok && "SET_CPU_ATTR failed!");	\
	} while (0)

#endif
