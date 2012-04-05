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

#define PRINT_TREE_INFO(v, ls) do {	\
	lsprintf(v, "Current instruction count %lu, total instructions %lu\n", \
		 ls->trigger_count, ls->absolute_trigger_count);	\
	lsprintf(v, "Total choice points %d, total backtracks %d\n",	\
		 ls->save.total_choices, ls->save.total_jumps);		\
	lsprintf(v, "Average instrs/choice %d, average branch depth %d\n", \
		 ls->save.total_triggers / (1+ls->save.total_choices),	\
		 ls->save.depth_total / (1+ls->save.total_jumps));	\
	} while (0)


void found_a_bug(struct ls_state *);

#endif
