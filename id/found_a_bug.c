/**
 * @file found_a_bug.c
 * @brief remembering which bugs have been found under which pp configs
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <pthread.h>

#include "array_list.h"
#include "common.h"
#include "pp.h"
#include "sync.h"
#include "xcalls.h"

static bool fab_inited = false;
static ARRAY_LIST(char *) fab_list;
static pthread_mutex_t fab_lock = PTHREAD_MUTEX_INITIALIZER;

static void check_init()
{
	if (!fab_inited) {
		LOCK(&fab_lock);
		if (!fab_inited) {
			ARRAY_LIST_INIT(&fab_list, 16);
			fab_inited = true;
		}
		UNLOCK(&fab_lock);
	}
}

void found_a_bug(char *trace_filename)
{
	check_init();

	LOCK(&fab_lock);
	ARRAY_LIST_APPEND(&fab_list, XSTRDUP(trace_filename));
	UNLOCK(&fab_lock);
}

bool found_any_bugs()
{
	unsigned int i;
	char **trace_filename;
	bool any = false;
	check_init();

	LOCK(&fab_lock);
	ARRAY_LIST_FOREACH(&fab_list, i, trace_filename) {
		ERR("Found a bug: %s\n", *trace_filename);
		any = true;
	}
	UNLOCK(&fab_lock);

	return any;
}
