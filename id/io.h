/**
 * @file io.h
 * @brief i/o routines for communicating with other landslide processes
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_IO_H
#define __ID_IO_H

#include <unistd.h>

struct file {
	int fd;
	char *filename;
};

bool create_file(struct file *f, const char *template);
void delete_file(struct file *f);

// FIXME: deal with short writes
#define WRITE(file, ...) do {						\
		char __buf[BUF_SIZE];					\
		int __len = scnprintf(__buf, BUF_SIZE, __VA_ARGS__);	\
		int __ret = write((file)->fd, __buf, __len);		\
		assert(__ret == __len && "failed write");		\
	} while (0)

#endif
