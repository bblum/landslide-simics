/**
 * @file common.h
 * @brief things common to all parts of landslide
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_COMMON_H
#define __LS_COMMON_H

#include <simics/api.h>
#include <stdio.h>

#ifndef MODULE_NAME
#error "Please define MODULE_NAME before including common.h!"
#endif

#ifndef MODULE_COLOUR
#define MODULE_COLOUR COLOUR_WHITE
#endif

#define COLOUR_BOLD "\033[01m"
#define COLOUR_DARK "\033[00m"
#define COLOUR_RED "\033[31m"
#define COLOUR_GREEN "\033[32m"
#define COLOUR_YELLOW "\033[33m"
#define COLOUR_BLUE "\033[34m"
#define COLOUR_MAGENTA "\033[35m"
#define COLOUR_CYAN "\033[36m"
#define COLOUR_WHITE "\033[38m"
#define COLOUR_DEFAULT "\033[00m"

/* Verbosity levels */
#define ALWAYS 0 /* yeah, uh */
#define BUG    ALWAYS /* stuff that must get printed to be ever useful */
#define BRANCH 1 /* once per branch - try not to have too many of these */
#define CHOICE 2 /* once per transition - really try to cut back on these */
#define DEV    3 /* messages only useful for development */
#define INFO   4 /* if you wouldn't put it anywhere but twitter */

#define MAX_VERBOSITY DEV

typedef int verbosity;

#define lsprintf(v, ...) do { if (v <= MAX_VERBOSITY) {			\
	fprintf(stderr, "\033[80D" COLOUR_BOLD MODULE_COLOUR		\
		"[" MODULE_NAME "]              " COLOUR_DEFAULT	\
		"\033[80D\033[16C" __VA_ARGS__);			\
	} } while (0)

#ifdef printf
#undef printf
#endif
#define printf(v, ...) do { if (v <= MAX_VERBOSITY) {	\
	fprintf(stderr, __VA_ARGS__);			\
	} } while (0)

#define MM_XMALLOC(x,t) ({				\
	typeof(t) *__ptr = MM_MALLOC(x,t);		\
	assert(__ptr != NULL && "malloc failed");	\
	__ptr; })

#define MM_XSTRDUP(s) ({				\
	char *__ptr = MM_STRDUP(s);			\
	assert(__ptr != NULL && "strdup failed");	\
	__ptr; })

#endif
