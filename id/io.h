/**
 * @file io.h
 * @brief i/o routines for communicating with other landslide processes
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_IO_H
#define __ID_IO_H

#define _GNU_SOURCE

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

// FIXME make more flexible
#define LANDSLIDE_PROGNAME "landslide"
#define LANDSLIDE_PATH "../pebsim"

struct file {
	int fd;
	char *filename;
};

void create_file(struct file *f, const char *template);
void delete_file(struct file *f, bool do_remove);

char *create_fifo(const char *prefix, unsigned int id);
void open_fifo(struct file *f, char *name, int flags);

void move_file_to(struct file *f, const char *dirpath);
void unset_cloexec(int fd);

#endif
