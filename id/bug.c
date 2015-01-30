/**
 * @file bug.c
 * @brief remembering which bugs have been found under which pp configs
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <pthread.h>

#include "array_list.h"
#include "common.h"
#include "job.h"
#include "pp.h"
#include "sync.h"
#include "xcalls.h"

struct bug_info {
	char *trace_filename;
	struct pp_set *config;
	char *log_filename;
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

void found_a_bug(char *trace_filename, struct job *j)
{
	struct bug_info b;
	b.trace_filename = XSTRDUP(trace_filename);
	b.config = clone_pp_set(j->config);
	b.log_filename = XSTRDUP(j->log_stderr.filename);

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
		       "Found a bug - %s - with PPs: ", b->trace_filename);
		print_pp_set(b->config, true);
		// FIXME: do something better than hardcode print "id/"
		printf(" (log file: id/%s)\n" COLOUR_DEFAULT, b->log_filename);
		any = true;
	}
	UNLOCK(&fab_lock);

	if (!any) {
		printf(COLOUR_BOLD COLOUR_GREEN
		       "No bugs were found -- you survived!\n");
	}

	return any;
}
