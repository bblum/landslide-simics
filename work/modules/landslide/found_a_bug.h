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

struct ls_state;

#define FOUND_A_BUG(ls, ...) do { 	\
		lsprintf(__VA_ARGS__);	\
		found_a_bug(ls);	\
	} while (0)

void found_a_bug(struct ls_state *);

#endif
