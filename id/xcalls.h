/**
 * @file xcalls.h
 * @brief convenience wrapper macros for syscalls
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_SYSCALL_H
#define __ID_SYSCALL_H

#include <errno.h>
#include <string.h>

#define EXPECT(cond, ...) do {						\
		int __last_errno = errno;				\
		if (!(cond)) {						\
			ERR("Assertion failed: '%s'\n", #cond);		\
			ERR(__VA_ARGS__);				\
			ERR("Error: %s\n", strerror(__last_errno));	\
		}							\
	} while (0)

// FIXME: convert to eval-argument-once form ("int __arg = (arg);")

#define XMALLOC(x,t) ({							\
	typeof(t) *__xmalloc_ptr = malloc((x) * sizeof(t));		\
	EXPECT(__xmalloc_ptr != NULL, "malloc failed");			\
	__xmalloc_ptr; })

#define XSTRDUP(s) ({							\
	char *__xstrdup_ptr = strndup(s, strlen(s));			\
	EXPECT(__xstrdup_ptr != NULL, "strdup failed");			\
	__xstrdup_ptr; })

#define FREE(x) free(x)

// FIXME: deal with short writes
#define XWRITE(file, ...) do {						\
		char __buf[BUF_SIZE];					\
		int __len = scnprintf(__buf, BUF_SIZE, __VA_ARGS__);	\
		int __ret = write((file)->fd, __buf, __len);		\
		EXPECT(__ret == __len, "failed write to file '%s'\n",	\
		       (file)->filename);				\
	} while (0)

#define XCLOSE(fd) do {							\
		int __ret = close(fd);					\
		EXPECT(__ret == 0, "failed close fd %d\n", fd);		\
	} while (0)

#define XREMOVE(filename) do {						\
		int __ret = remove(filename);				\
		EXPECT(__ret == 0, "failed remove '%s'\n", (filename));	\
	} while (0)

#define XPIPE(pipefd) do {						\
		int __ret = pipe2(pipefd, O_CLOEXEC);			\
		EXPECT(__ret == 0, "failed create pipe\n");		\
	} while (0)

#define XCHDIR(path) do {						\
		int __ret = chdir(path);				\
		EXPECT(__ret == 0, "failed chdir to '%s'\n", (path));	\
	} while (0)

#define XRENAME(oldpath, newpath) do {					\
		int __ret = rename((oldpath), (newpath));		\
		EXPECT(__ret == 0, "failed rename '%s' to '%s'\n",	\
		       (oldpath), (newpath));				\
	} while (0)

#define XDUP2(oldfd, newfd) do {					\
		int __newfd = (newfd);					\
		int __ret = dup2((oldfd), __newfd);			\
		EXPECT(__ret == __newfd, "failed dup2 %d <- %d\n",	\
		       (oldfd), __newfd);				\
	} while (0)

#endif
