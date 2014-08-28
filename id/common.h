/**
 * @file common.h
 * @brief things common to all parts of landslide
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_COMMON_H
#define __ID_COMMON_H

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* See work/modules/landslide/compiler.h */

#define NORETURN __attribute__((noreturn))
#define MUST_CHECK __attribute__((warn_unused_result))
#define MAYBE_UNUSED __attribute__((unused))

#define STATIC_ZERO_ASSERT(condition) (sizeof(struct { int:-!(condition); }))
#define STATIC_NULL_ASSERT(condition) ((void *)STATIC_ZERO_ASSERT(condition))

/* Force a compilation error if condition is false */
#define STATIC_ASSERT(condition) ((void)STATIC_ZERO_ASSERT(condition))

/* Force a compilation error if a constant expression is not a power of 2 */
#define STATIC_ASSERT_POWER_OF_2(n)          \
	STATIC_ASSERT(!((n) == 0 || (((n) & ((n) - 1)) != 0)))

/* Type and array checking */
#define SAME_TYPE(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define MUST_BE_ARRAY(a) STATIC_ZERO_ASSERT(!SAME_TYPE((a), &(a)[0]))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + MUST_BE_ARRAY(arr))

/* ANSI color codes */

#define COLOUR_BOLD "\033[01m"
#define COLOUR_DARK "\033[00m"
#define COLOUR_RED "\033[31m"
#define COLOUR_GREEN "\033[32m"
#define COLOUR_YELLOW "\033[33m"
#define COLOUR_BLUE "\033[34m"
#define COLOUR_MAGENTA "\033[35m"
#define COLOUR_CYAN "\033[36m"
#define COLOUR_GREY "\033[37m"
#define COLOUR_WHITE "\033[38m"
#define COLOUR_DEFAULT "\033[00m"

#define BUF_SIZE 256 /* default length for internal print buffers */

#define typeof __typeof__

#define ERR(...) do {							\
		fprintf(stderr, COLOUR_BOLD COLOUR_RED __VA_ARGS__);	\
		fprintf(stderr, COLOUR_DEFAULT);			\
	} while (0)

#define WARN(...) do {							\
		fprintf(stderr, COLOUR_BOLD COLOUR_YELLOW __VA_ARGS__);	\
		fprintf(stderr, COLOUR_DEFAULT);			\
	} while (0)

/* Debug flag, controls info printouts not useful for ze studence */
#define DEBUG

#ifdef DEBUG
#define DBG(...) do {							\
		fprintf(stderr, COLOUR_DARK COLOUR_CYAN __VA_ARGS__);	\
		fprintf(stderr, COLOUR_DEFAULT);			\
	} while (0)
#else
#define DBG(...)
#endif

/* you couldn't make this shit up if you tried */
#define scnprintf(buf, maxlen, ...) ({					\
	int __snprintf_ret = snprintf((buf), (maxlen), __VA_ARGS__);	\
	if (__snprintf_ret > (int)(maxlen)) {				\
		__snprintf_ret = (maxlen);				\
	}								\
	__snprintf_ret; })

#endif
