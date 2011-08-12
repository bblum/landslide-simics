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

/******************************************************************************
 * interface
 ******************************************************************************/

/* internally: always assume directory strings don't have a "/" at the end */

void save_init(struct save_state *ss, char *base_dir)
{
	int len = strlen(base_dir) * 2; /* start with some padding */

	ss->dir.path = MM_MALLOC(len, char);
	assert(ss->dir.path && "failed to allocate ss->dir.path");
	strcpy(ss->dir.path, base_dir);
	ss->dir.len = len;

	ss->last_choice.live = false;
}

void save_choice(struct save_state *ss, int eip, int tid)
{
	assert(!ss->last_choice.live &&
	       "can't save new choice over existing uncommitted one");
	ss->last_choice.eip = eip;
	ss->last_choice.tid = tid;
	ss->last_choice.live = true;
}

/* pass NULL as the ls_state to indicate the test case ended */
void save_choice_commit(struct save_state *ss, struct ls_state *ls)
{
	char *buf;
	BUILD_BUG_ON(MAX_TID_LEN < MAX(strlen("/eip"), strlen("/tids")));

	assert(ss->last_choice.live && "can't commit a nonexistent choice");
	
	/* committing a choice that causes future choices */
	if (ls) {
		int ret;
		struct agent *a;

		/* append the tid onto the buf */
		while (strlen(ss->dir.path) + MAX_TID_LEN >= ss->dir.len) {
			char *path = MM_MALLOC(ss->dir.len * 2, char);
			assert(path && "failed to reallocate ss->dir.path");
			strcpy(path, ss->dir.path);
			MM_FREE(ss->dir.path);
			ss->dir.path = path;
			ss->dir.len *= 2;
		}

		/* allocate extra for the sub-files */
		buf = MM_MALLOC(ss->dir.len + MAX_TID_LEN, char);
		assert(buf && "failed allocate temp buf");
		snprintf(buf, MAX_TID_LEN, "/%d", ss->last_choice.tid);
		strcat(ss->dir.path, buf);

		/* create directory */
		ret = mkdir(ss->dir.path, DIR_MODE);
		assert(ret == 0 && "failed to mkdir");
		
		/* init metadata - eip */
		snprintf(buf, ss->dir.len + MAX_TID_LEN,
			 "%s/eip", ss->dir.path);
		make_empty_file(buf);
		write_num_to_file(buf, ls->eip);
		/* init metadata - tids */
		snprintf(buf, ss->dir.len + MAX_TID_LEN,
			 "%s/tids", ss->dir.path);
		make_empty_file(buf);
		Q_FOREACH(a, &ls->sched.rq, nobe) {
			write_num_to_file(buf, a->tid);
		}
		Q_FOREACH(a, &ls->sched.sq, nobe) {
			write_num_to_file(buf, a->tid);
		}

	/* committing a terminal choice */
	} else {
		buf = MM_MALLOC(ss->dir.len + MAX_TID_LEN, char);
		assert(buf && "failed allocate temp buf");
		snprintf(buf, MAX_TID_LEN, "/%d", ss->last_choice.tid);
		make_empty_file(buf);
	}

	ss->last_choice.live = false;
}
