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
#include "rand.h"
#include "save.h"
#include "schedule.h"
#include "test.h"
#include "user_sync.h"

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
	struct mem_state kern_mem;
	struct mem_state user_mem;
	struct user_sync_state user_sync;
	struct rand_state rand;

	char *cmd_file;

	bool just_jumped;
};

#define LS_NO_KNOWN_BUG 0
#define LS_BUG_FOUND 1

conf_object_t *get_symtable();
void set_symtable(conf_object_t *symtable);
int symtable_lookup(char *buf, int maxlen, int addr, bool *unknown);
int symtable_lookup_data(char *buf, int maxlen, int addr);
bool function_eip_offset(int eip, int *offset);
bool find_user_global_of_type(const char *typename, int *size_result);

#endif
