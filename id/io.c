/**
 * @file io.c
 * @brief i/o routines for communicating with other landslide processes
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "io.h"

#define CONFIG_FILE_TEMPLATE "config-extra-XXXXXXXXXXXXXXXX.landslide"
#define RESULTS_FILE_TEMPLATE "results-XXXXXXXXXXXXXXXX.landslide"

/* returns a malloced string */
bool create_file(const char *template, struct file *f)
{
	f->filename = XMALLOC(strlen(template) + 1, char);
	strcpy(f->filename, template);

	int ret = mkstemp(f->filename);
	if (ret != 0) {
		return false;
	}

	f->fd = open(f->filename, O_APPEND);
	if (f->fd <= 0) {
		ret = remove(f->filename);
		assert(ret == 0 && "couldn't remove file");
		return false;
	}

	return true;
}

bool create_config_file(struct file *f)
{
	return create_file(CONFIG_FILE_TEMPLATE, f);
}

bool create_results_file(struct file *f)
{
	return create_file(RESULTS_FILE_TEMPLATE, f);
}

/* closes fd, removes file from filesystem, frees filename string */
void delete_file(struct file *f)
{
	assert(f->filename != NULL);
	int ret = close(f->fd);
	assert(ret == 0 && "couldn't close fd");
	ret = remove(f->filename);
	assert(ret == 0 && "couldn't remove file");
	FREE(f->filename);
	f->filename = NULL;
}
