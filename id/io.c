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
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "io.h"
#include "xcalls.h"

/* returns a malloced string */
void create_file(struct file *f, const char *template)
{
	f->filename = XMALLOC(strlen(template) + 1, char);
	strcpy(f->filename, template);

	f->fd = mkostemp(f->filename, O_APPEND | O_CLOEXEC);
	assert(f->fd >= 0 && "failed create file");
}

/* closes fd, removes file from filesystem, frees filename string */
void delete_file(struct file *f, bool do_remove)
{
	assert(f->filename != NULL);
	XCLOSE(f->fd);
	if (do_remove) {
		XREMOVE(f->filename);
	}
	FREE(f->filename);
	f->filename = NULL;
}

static unsigned long timestamp()
{
	struct timeval tv;
	int rv = gettimeofday(&tv, NULL);
	assert(rv == 0 && "failed gettimeofday");
	return (unsigned long)((tv.tv_sec * 1000000) + tv.tv_usec);
}

/* can't create fifos in AFS; use ramdisk instead */
#define FIFO_DIR "/dev/shm/"

/* returns a malloced string */
char *create_fifo(const char *prefix, unsigned int id)
{
	/* assemble filename */
	char buf[BUF_SIZE];
	scnprintf(buf, BUF_SIZE, FIFO_DIR "%s-%u-%lu.fifo",
		  prefix, id, timestamp());

	/* create file */
	int ret = mkfifo(buf, 0700);
	assert(ret == 0 && "failed create fifo file");

	return XSTRDUP(buf);
}

/* name: a malloced string returned by create_fifo */
void open_fifo(struct file *f, char *name, int flags)
{
	f->filename = name;

	/* obtain file descriptor in appropriate mode */
	f->fd = open(f->filename, flags | O_CLOEXEC);
	assert(f->fd >= 0 && "failed open fifo file");
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
