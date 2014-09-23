/**
 * @file found_a_bug.h
 * @brief function for dumping debug info and quitting simics when finding a bug
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_FOUND_A_BUG_H
#define __LS_FOUND_A_BUG_H

#ifndef __LS_COMMON_H
#error "must include common.h before found_a_bug.h"
#endif

#include <inttypes.h> /* for PRIu64 */
#include <unistd.h> /* for write */

#include "landslide.h"
#include "stack.h"

struct ls_state;

#define _PRINT_TREE_INFO(v, mn, mc, ls) do {				\
	_lsprintf(v, mn, mc, "Current instruction count %" PRIu64 ", "	\
		  "total instructions %" PRIu64 "\n", 			\
		  ls->trigger_count, ls->absolute_trigger_count);	\
	_lsprintf(v, mn, mc, "Total preemption-points %" PRIu64 ", "	\
		  "total backtracks %" PRIu64 "\n", 			\
		  ls->save.total_choices, ls->save.total_jumps);	\
	_lsprintf(v, mn, mc,						\
		  "Average instrs/preemption-point %lu, "		\
		  "average branch depth %lu\n",				\
		  ls->save.total_triggers / (1+ls->save.total_choices),	\
		  ls->save.depth_total / (1+ls->save.total_jumps));	\
	} while (0)

#define PRINT_TREE_INFO(v, ls) \
	_PRINT_TREE_INFO(v, MODULE_NAME, MODULE_COLOUR, ls)

/* gross C glue for enabling the third-order-function-style macro below. */
struct fab_html_env {
	int html_fd;
};
typedef void (*fab_cb_t)(struct fab_html_env *env);

void _found_a_bug(struct ls_state *, bool bug_found, bool verbose,
		  const char *reason, unsigned int reason_len, fab_cb_t callback);

#define DUMP_DECISION_INFO(ls) \
	_found_a_bug(ls, false, true,  NULL, 0, NULL) // Verbose
#define DUMP_DECISION_INFO_QUIET(ls) \
	_found_a_bug(ls, false, false, NULL, 0, NULL) // Not

/* Simple common interface. */
#define FOUND_A_BUG(ls, ...) do { 						\
		char __fab_buf[1024];						\
		int __fab_len = scnprintf(__fab_buf, 1024, __VA_ARGS__);	\
		_found_a_bug(ls, true, false, __fab_buf, __fab_len, NULL);	\
	} while (0)

// FIXME: Find a clean way to move this stuff to html.h
#define HTML_BUF_LEN 4096
#define HTML_PRINTF(env, ...) do {						\
		char __html_buf[HTML_BUF_LEN];					\
		unsigned int __len =						\
			scnprintf(__html_buf, HTML_BUF_LEN, __VA_ARGS__);	\
		assert(__len > 0 && "failed scnprintf");			\
		unsigned int __ret = write((env)->html_fd, __html_buf, __len);	\
		assert(__ret > 0 && "failed write");				\
	} while(0)
#define HTML_PRINT_STACK_TRACE(env, st) do {					\
		char __html_buf[HTML_BUF_LEN];					\
		unsigned int __len =						\
			html_stack_trace(__html_buf, HTML_BUF_LEN, st);		\
		unsigned int __ret = write((env)->html_fd, __html_buf, __len);	\
		assert(__ret > 0 && "failed write");				\
	} while (0)

/* 3rd-order-style function for getting extra info into html trace output.
 * Binds 'env_var_name' as the opaque environment packet. */
#define FOUND_A_BUG_HTML_INFO(ls, reason, reason_len, env_var_name, code)	\
	do {									\
		void __cb(struct fab_html_env *env_var_name) { code }		\
		_found_a_bug(ls, true, false, reason, reason_len, __cb);	\
	} while (0)

#endif
