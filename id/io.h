/**
 * @file io.h
 * @brief i/o routines for communicating with other landslide processes
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_IO_H
#define __ID_IO_H

struct file {
	int fd;
	char *filename;
};

bool create_config_file(struct file *f);
bool create_results_file(struct file *f);
void delete_file(struct file *f);

// FIXME: deal with short writes
#define WRITE(fd, ...) do {						\
		char __buf[BUF_SIZE];					\
		int __len = scnprintf(__buf, BUF_SIZE, __VA_ARGS__);	\
		int __ret = write(fd, __buf, __len);			\
		assert(__ret == __len && "failed write");		\
	} while (0)

#endif
