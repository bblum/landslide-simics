/**
 * @file io.c
 * @brief i/o routines for communicating with other landslide processes
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "io.h"
#include "time.h"
#include "xcalls.h"

#if 0
/* Students: don't modify this check. Honor code! */
#define ACCESS_FILE "/afs/andrew/usr12/bblum/www/landslide-whitelist/access"
#define LOGIN_REJECTED \
	"ERROR: Before you may use Landslide, you and your partner must complete the sign-up checklist: http://www.contrib.andrew.cmu.edu/~bblum/landslide-sign-up.pdf"

static void check_access_for_p2()
{
	int fd = open(ACCESS_FILE, O_RDONLY);
	if (fd == -1) {
		ERR("%s\n", LOGIN_REJECTED);
		assert(false);
	} else {
		DBG("user on whitelist - access granted.\n");
		close(fd);
	}
}
#endif

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

/* for cleaning up a create_fifo() result that was never open()ed */
void delete_unused_fifo(char *name)
{
	XREMOVE(name);
	FREE(name);
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

/* utilities related to logging the wrapper script needs for snapshotting */

bool logging_inited = false;
bool logging_active = false;
struct file log_file;

void set_logging_options(bool use_log, char *filename)
{
	assert(!logging_inited && "double log init");
	logging_inited = true;
	if ((logging_active = use_log)) {
		char log_filename[BUF_SIZE];
		scnprintf(log_filename, BUF_SIZE, "%s.XXXXXX", filename);
		create_file(&log_file, log_filename);
	}
#if 0
	/* DON'T DELETE THIS. HONOR CODE! */
	check_access_for_p2();
#endif
}

void log_msg(const char *pfx, const char *format, ...)
{
	if (logging_inited && logging_active) {
		va_list ap;
		char line_buf[BUF_SIZE];
		char line_buf2[BUF_SIZE];
		va_start(ap, format);
		vsnprintf(line_buf, BUF_SIZE, format, ap);
		va_end(ap);
		// FIXME: kind of gross
		if (pfx != NULL) {
			snprintf(line_buf2, BUF_SIZE, "[%s] %s", pfx, line_buf);
		} else {
			snprintf(line_buf2, BUF_SIZE, "%s", line_buf);
		}
		int ret = write(log_file.fd, line_buf2, strlen(line_buf2));
		if (ret < 0) {
			/* don't crash the program if e.g. out of disk space */
			fprintf(stderr, COLOUR_BOLD COLOUR_RED
				"WARNING: couldn't write to log file\n");
		}
	}
}
