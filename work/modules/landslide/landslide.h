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
#include "messaging.h"
#include "pp.h"
#include "rand.h"
#include "save.h"
#include "schedule.h"
#include "test.h"
#include "user_sync.h"

struct ls_state {
	/* log_object_t must be the first thing in the device struct */
	log_object_t log;

	uint64_t trigger_count; /* in this branch of the tree */
	uint64_t absolute_trigger_count; /* in the whole execution */

	/* Pointers to relevant objects. Currently only supports one CPU. */
	conf_object_t *cpu0;
	conf_object_t *kbd0;
	unsigned int eip;
	uint8_t instruction_text[16];

	struct sched_state sched;
	struct arbiter_state arbiter;
	struct save_state save;
	struct test_state test;
	struct mem_state kern_mem;
	struct mem_state user_mem;
	struct user_sync_state user_sync;
	struct rand_state rand;
	struct messaging_state mess;
	struct pp_config pps;

	char *cmd_file;
	char *html_file;

	bool just_jumped;
};

/* process exit codes */
#define LS_NO_KNOWN_BUG 0
#define LS_BUG_FOUND 1
#define LS_ASSERTION_FAILED 2

/* for simics glue */
struct ls_state *new_landslide();
void landslide_entrypoint(conf_object_t *obj, void /* trace_entry_t */ *entry);

#endif
