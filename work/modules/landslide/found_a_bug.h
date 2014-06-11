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

#include "landslide.h"

struct ls_state;

#define _PRINT_TREE_INFO(v, mn, mc, ls) do {				\
	_lsprintf(v, mn, mc,						\
		  "Current instruction count %lu, total instructions %lu\n", \
		  ls->trigger_count, ls->absolute_trigger_count);	\
	_lsprintf(v, mn, mc, "Total preemption-points %d, total backtracks %d\n", \
		  ls->save.total_choices, ls->save.total_jumps);	\
	_lsprintf(v, mn, mc,						\
		  "Average instrs/preemption-point %d, average branch depth %d\n", \
		  ls->save.total_triggers / (1+ls->save.total_choices),	\
		  ls->save.depth_total / (1+ls->save.total_jumps));	\
	} while (0)

#define PRINT_TREE_INFO(v, ls) \
	_PRINT_TREE_INFO(v, MODULE_NAME, MODULE_COLOUR, ls)

void _found_a_bug(struct ls_state *, bool bug_found, bool verbose,
				  char *reason, int reason_len);

#define FOUND_A_BUG(ls, ...) do { 									\
		char __fab_buf[1024];										\
		int __fab_len = scnprintf(__fab_buf, 1024, __VA_ARGS__);	\
		_found_a_bug(ls, true, false, __fab_buf, __fab_len);		\
	} while (0)

#define DUMP_DECISION_INFO(ls) _found_a_bug(ls, false, true, NULL, 0)        // Verbose
#define DUMP_DECISION_INFO_QUIET(ls) _found_a_bug(ls, false, false, NULL, 0) // Not

#endif
