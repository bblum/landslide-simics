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

#define lsprintf(...)							\
	fprintf(stderr, "\033[80D" COLOUR_BOLD MODULE_COLOUR		\
		"[" MODULE_NAME "]              " COLOUR_DEFAULT	\
		"\033[80D\033[16C" __VA_ARGS__);

#ifdef printf
#undef printf
#endif
#define printf(...) fprintf(stderr, __VA_ARGS__);

#define MM_XMALLOC(x,t) ({				\
	typeof(t) *__ptr = MM_MALLOC(x,t);		\
	assert(__ptr != NULL && "malloc failed");	\
	__ptr; })

#endif
