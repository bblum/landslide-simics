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

#define FOUND_A_BUG(ls, ...) do { 	\
		lsprintf(__VA_ARGS__);	\
		found_a_bug(ls);	\
	} while (0)

#define _PRINT_TREE_INFO(v, mn, mc, ls) do {				\
	_lsprintf(v, mn, mc,						\
		  "Current instruction count %lu, total instructions %lu\n", \
		  ls->trigger_count, ls->absolute_trigger_count);	\
	_lsprintf(v, mn, mc, "Total decision points %d, total backtracks %d\n", \
		  ls->save.total_choices, ls->save.total_jumps);	\
	_lsprintf(v, mn, mc,						\
		  "Average instrs/decision %d, average branch depth %d\n", \
		  ls->save.total_triggers / (1+ls->save.total_choices),	\
		  ls->save.depth_total / (1+ls->save.total_jumps));	\
	} while (0)

#define PRINT_TREE_INFO(v, ls) \
	_PRINT_TREE_INFO(v, MODULE_NAME, MODULE_COLOUR, ls)

#define found_a_bug(ls) _found_a_bug(ls, true)
#define dump_decision_info(ls) _found_a_bug(ls, false)

void _found_a_bug(struct ls_state *, bool);

#endif
