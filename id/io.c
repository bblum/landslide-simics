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

/* returns a malloced string */
bool create_config_file(int *fd, char **filename)
{
	*filename = XMALLOC(strlen(CONFIG_FILE_TEMPLATE) + 1, char);
	strcpy(*filename, CONFIG_FILE_TEMPLATE);

	int ret = mkstemp(*filename);
	if (ret != 0) {
		return false;
	}

	*fd = open(*filename, O_APPEND);
	if (*fd <= 0) {
		ret = remove(*filename);
		assert(ret == 0 && "couldn't remove file");
		return false;
	}

	// TODO
	//WRITE(*fd, "%s\n", pp_lock.config_str);
	//WRITE(*fd, "%s\n", pp_unlock.config_str);
	return true;
}

/* closes fd, removes file from filesystem, frees filename string */
void delete_config_file(int fd, char *filename)
{
	int ret = close(fd);
	assert(ret == 0 && "couldn't close fd");
	ret = remove(filename);
	assert(ret == 0 && "couldn't remove file");
	FREE(filename);
}
