/**
 * @file landslide.h
 * @brief common landslide stuff
 * @author Ben Blum
 */

#ifndef __LS_LANDSLIDE_H
#define __LS_LANDSLIDE_H

#include <simics/api.h>

#include "schedule.h"

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
};

#endif
