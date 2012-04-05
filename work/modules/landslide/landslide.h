/**
 * @file landslide.h
 * @brief common landslide stuff
 * @author Ben Blum
 */

#ifndef __LS_LANDSLIDE_H
#define __LS_LANDSLIDE_H

#include <simics/api.h>

#include "arbiter.h"
#include "memory.h"
#include "save.h"
#include "schedule.h"
#include "test.h"

#define SIM_MODULE_NAME "landslide"

struct ls_state {
	/* log_object_t must be the first thing in the device struct */
	log_object_t log;

	unsigned long trigger_count; /* in this branch of the tree */
	unsigned long absolute_trigger_count; /* in the whole execution */

	/* Pointers to relevant objects. Currently only supports one CPU. */
	conf_object_t *cpu0;
	conf_object_t *kbd0;
	int eip;

	struct sched_state sched;
	struct arbiter_state arbiter;
	struct save_state save;
	struct test_state test;
	struct mem_state mem;

	char *cmd_file;

	bool just_jumped;
};

#define LS_NO_KNOWN_BUG 0
#define LS_BUG_FOUND 1

int symtable_lookup(char *buf, int maxlen, int addr);
bool function_eip_offset(int eip, int *offset);

#endif
