/**
 * @file io.c
 * @brief i/o routines for communicating with other landslide processes
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "io.h"

/* returns a malloced string */
bool create_file(struct file *f, const char *template)
{
	f->filename = XMALLOC(strlen(template) + 1, char);
	strcpy(f->filename, template);

	int ret = mkostemp(f->filename, O_APPEND | O_CLOEXEC);
	if (ret < 0) {
		return false;
	}

	f->fd = ret;
	return true;
}

/* closes fd, removes file from filesystem, frees filename string */
void delete_file(struct file *f)
{
	assert(f->filename != NULL);
	XCLOSE(f->fd);
	XREMOVE(f->filename);
	FREE(f->filename);
	f->filename = NULL;
}

void unset_cloexec(int fd)
{
	/* communication pipes were opened with CLOEXEC set so as not to race
	 * with simultaneous fork-execs of other jobs. "reenable" them here. */
	int flags = fcntl(fd, F_GETFD);
	assert(flags != -1 && "couldn't get flags to unset cloexec");
	int result = fcntl(fd, F_SETFD, flags & ~O_CLOEXEC);
	assert(result == 0 && "couldn't set flags to unset cloexec");
}

void move_file_to(struct file *f, const char *dirpath)
{
	unsigned int length = strlen(f->filename) + strlen(dirpath) + 2;
	char *new_filename = XMALLOC(length, char);
	scnprintf(new_filename, length, "%s/%s", dirpath, f->filename);
	XRENAME(f->filename, new_filename);
	FREE(f->filename);
	f->filename = new_filename;
}
