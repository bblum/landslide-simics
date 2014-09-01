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

struct bug_info {
	char *filename;
	struct pp_set *config;
};

static bool fab_inited = false;
static ARRAY_LIST(struct bug_info) fab_list;
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

void found_a_bug(char *trace_filename, struct pp_set *config)
{
	struct bug_info b;
	b.filename = XSTRDUP(trace_filename);
	b.config = clone_pp_set(config);

	check_init();

	LOCK(&fab_lock);
	ARRAY_LIST_APPEND(&fab_list, b);
	UNLOCK(&fab_lock);
}

/* Did a prior job with a subset of the given PPs already find a bug? */
bool bug_already_found(struct pp_set *config)
{
	unsigned int i;
	struct bug_info *b;
	bool result = false;

	check_init();

	LOCK(&fab_lock);
	ARRAY_LIST_FOREACH(&fab_list, i, b) {
		if (pp_subset(b->config, config)) {
			result = true;
			break;
		}
	}
	UNLOCK(&fab_lock);

	return result;
}

bool found_any_bugs()
{
	unsigned int i;
	struct bug_info *b;
	bool any = false;
	check_init();

	LOCK(&fab_lock);
	ARRAY_LIST_FOREACH(&fab_list, i, b) {
		printf(COLOUR_BOLD COLOUR_RED
		       "Found a bug - %s - with PPs: ", b->filename);
		print_pp_set(b->config);
		printf("\n" COLOUR_DEFAULT);
		any = true;
	}
	UNLOCK(&fab_lock);

	return any;
}
