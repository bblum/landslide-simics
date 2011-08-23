/**
 * @file save.c
 * @brief facility for saving and restoring arbiter choice trees
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <simics/alloc.h>

#define MODULE_NAME "SAVE"
#define MODULE_COLOUR COLOUR_MAGENTA

#include "common.h"
#include "compiler.h"
#include "landslide.h"
#include "save.h"
#include "schedule.h"

#define MAX_TID_LEN strlen("/4294967296")

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define DIR_MODE (FILE_MODE | S_IXUSR | S_IXGRP | S_IXOTH)

/******************************************************************************
 * helpers
 ******************************************************************************/

static void make_empty_file(char *name)
{
	FILE *f = fopen(name, "w+");
	assert(f && "failed fopen empty file");
	fclose(f);
}

static void write_num_to_file(char *name, int num)
{
	int ret;
	FILE *f = fopen(name, "a");
	assert(f && "failed fopen existing file");
	ret = fprintf(f, "%d\n", num);
	assert(ret > 0 && "failed fprintf");
	fclose(f);
}

static void write_eip_file(struct save_state *ss, int eip)
{
	int len = ss->dir.len + strlen("/eip") + 1;
	char *buf = MM_MALLOC(len, char);
	assert(buf && "failed allocate temp buf");

	snprintf(buf, len, "%s/eip", ss->dir.path);
	make_empty_file(buf);
	write_num_to_file(buf, eip);
	MM_FREE(buf);
}

static void write_tids_file(struct save_state *ss, struct sched_state *s)
{
	struct agent *a;
	int len = ss->dir.len + strlen("/tids") + 1;
	char *buf = MM_MALLOC(len, char);
	assert(buf && "failed allocate temp buf");

	snprintf(buf, len, "%s/tids", ss->dir.path);
	make_empty_file(buf);
	Q_FOREACH(a, &s->rq, nobe) {
		write_num_to_file(buf, a->tid);
	}
	Q_FOREACH(a, &s->sq, nobe) {
		write_num_to_file(buf, a->tid);
	}
	MM_FREE(buf);
}

static void save_append_tid(struct save_state *ss, int tid)
{
	char subdir[MAX_TID_LEN + 1];

	while (strlen(ss->dir.path) + MAX_TID_LEN + 1 > ss->dir.len) {
		char *path = MM_MALLOC(ss->dir.len * 2, char);
		assert(path && "failed to reallocate ss->dir.path");
		strcpy(path, ss->dir.path);
		MM_FREE(ss->dir.path);
		ss->dir.path = path;
		ss->dir.len *= 2;
	}
	snprintf(subdir, MAX_TID_LEN + 1, "/%d", tid);
	strcat(ss->dir.path, subdir);
}

/******************************************************************************
 * interface
 ******************************************************************************/

/* internally: always assume directory strings don't have a "/" at the end */

/* called with base_dir as the directory for the first new choice to be made */
void save_init(struct save_state *ss)
{
	ss->dir.path = NULL;
	ss->last_choice.live = false;
	ss->save_choice_ever_called = false;
}

bool save_set_base_dir(struct save_state *ss, const char *base_dir)
{
	int len = strlen(base_dir) * 2; /* start with some padding */

	if (ss->dir.path) {
		lsprintf("Cannot set save path if it's already set.\n");
		return false;
	}

	if (ss->save_choice_ever_called) {
		lsprintf("Too late to set save path.\n");
		return false;
	}

	ss->dir.path = MM_MALLOC(len, char);
	assert(ss->dir.path && "failed to allocate ss->dir.path");
	strcpy(ss->dir.path, base_dir);
	ss->dir.len = len;
	return true;
}

const char *save_get_path(struct save_state *ss)
{
	return ss->dir.path ? ss->dir.path : "/dev/null"; /* c.c */
}

void save_choice(struct save_state *ss, int eip, int tid)
{
	if (!ss->dir.path) {
		lsprintf("not saving choice %d at 0x%x\n", tid, eip);
		return;
	}

	assert(!ss->last_choice.live &&
	       "can't save new choice over existing uncommitted one");
	ss->last_choice.eip = eip;
	ss->last_choice.tid = tid;
	ss->last_choice.live = true;
	ss->save_choice_ever_called = true;
}

/* Call to "commit" a new directory representing a choice we just encountered.
 * pass NULL as the ls_state to indicate the test case ended */
void save_choice_commit(struct save_state *ss, struct ls_state *ls, bool our_choice)
{
	if (!ss->dir.path) {
		lsprintf("not committing...\n");
		return;
	}

	/* committing a choice that causes future choices */
	if (ls) {
		if (ss->last_choice.live) {
			assert(ss->save_choice_ever_called);

			/* append the tid onto the buf (descend directory) */
			save_append_tid(ss, ss->last_choice.tid);

			if (our_choice) {
				/* create directory */
				int ret = mkdir(ss->dir.path, DIR_MODE);
				assert(ret == 0 && "failed to mkdir");
			}
		} else {
			/* This may be called once before save_choice() to init
			 * the base directory. */
			assert(!ss->save_choice_ever_called);
			ss->save_choice_ever_called = true;
		}
		/* in either case, we write metadata iff it was our choice. */
		if (our_choice) {
				/* init metadata */
				write_eip_file(ss, ls->eip);
				write_tids_file(ss, &ls->sched);
		}
		
	/* committing a terminal choice */
	} else {
		char *buf = MM_MALLOC(ss->dir.len + MAX_TID_LEN + 1, char);
		assert(buf && "failed allocate temp buf");
		snprintf(buf, MAX_TID_LEN, "/%d", ss->last_choice.tid);
		make_empty_file(buf);
		MM_FREE(buf);
	}

	ss->last_choice.live = false;
}
