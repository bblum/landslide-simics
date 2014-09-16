/**
 * @file common.h
 * @brief things common to all parts of landslide
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_COMMON_H
#define __LS_COMMON_H

#include <simics/api.h>
#include <stdio.h>
#include <assert.h>

#include "student_specifics.h" // for verbosity

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
#define COLOUR_GREY "\033[37m"
#define COLOUR_WHITE "\033[38m"
#define COLOUR_DEFAULT "\033[00m"

#define BUF_SIZE 256 /* default length for internal print buffers */

/* Verbosity levels */
#define ALWAYS 0 /* yeah, uh */
#define BUG    ALWAYS /* stuff that must get printed to be ever useful */
#define BRANCH 1 /* once per branch - try not to have too many of these */
#define CHOICE 2 /* once per transition - really try to cut back on these */
#define DEV    3 /* messages only useful for development */
#define INFO   4 /* if you wouldn't put it anywhere but twitter */

#if VERBOSE == 0 && EXTRA_VERBOSE == 0
#define MAX_VERBOSITY CHOICE // default
#elif EXTRA_VERBOSE != 0
#define MAX_VERBOSITY INFO // EXTRA_VERBOSE overrides VERBOSE
#else
#define MAX_VERBOSITY DEV
#endif

typedef int verbosity;

// Can be called directly if you want to override the module name and colour.
#define _lsprintf(v, mn, mc, ...) do { if ((v) <= MAX_VERBOSITY) {	\
	fprintf(stderr, "\033[80D" COLOUR_BOLD mc "[" mn "]              " \
		COLOUR_DEFAULT "\033[80D\033[16C" __VA_ARGS__);		\
	} } while (0)

#define lsprintf(v, ...) _lsprintf((v), MODULE_NAME, MODULE_COLOUR, __VA_ARGS__)

#ifdef printf
#undef printf
#endif
#define printf(v, ...) do { if ((v) <= MAX_VERBOSITY) {	\
		fprintf(stderr, __VA_ARGS__);					\
	} } while (0)

/* Specialized prints that are only emitted when in kernel- or user-space */
bool testing_userspace();
#define lsuprintf(...) \
	do { if ( testing_userspace()) { lsprintf(__VA_ARGS__); } } while (0)
#define lskprintf(...) \
	do { if (!testing_userspace()) { lsprintf(__VA_ARGS__); } } while (0)
#define uprintf(...) \
	do { if ( testing_userspace()) {   printf(__VA_ARGS__); } } while (0)
#define kprintf(...) \
	do { if (!testing_userspace()) {   printf(__VA_ARGS__); } } while (0)

/* Custom assert */
#ifdef assert
#undef assert
#endif

extern void landslide_assert_fail(const char *message, const char *file,
				  unsigned int line, const char *function);
#define assert(expr) do { if (!(expr)) {				\
		landslide_assert_fail(__STRING(expr), __FILE__,		\
				      __LINE__, __ASSERT_FUNCTION);	\
	} } while (0)

/* you couldn't make this shit up if you tried */
#define scnprintf(buf, maxlen, ...) ({								\
	int __snprintf_ret = snprintf((buf), (maxlen), __VA_ARGS__);	\
	if (__snprintf_ret > (maxlen)) { __snprintf_ret = (maxlen); }	\
	__snprintf_ret; })

#define MM_XMALLOC(x,t) ({								\
	typeof(t) *__xmalloc_ptr = MM_MALLOC(x,t);			\
	assert(__xmalloc_ptr != NULL && "malloc failed");	\
	__xmalloc_ptr; })

#define MM_XSTRDUP(s) ({								\
	char *__xstrdup_ptr = MM_STRDUP(s);					\
	assert(__xstrdup_ptr != NULL && "strdup failed");	\
	__xstrdup_ptr; })

#define HURDLE_VIOLATION(msg) do { hurdle_violation(msg); assert(0); } while (0)
static inline void hurdle_violation(const char *msg) {
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED
		 "/!\\ /!\\ /!\\ HURDLE VIOLATION /!\\ /!\\ /!\\\n");
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "%s\n", msg);
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_RED "Landslide can probably cope "
		 " with this, but instead of continuing to use Landslide, please"
		 " go fix your kernel.\n" COLOUR_DEFAULT);
}

#endif
