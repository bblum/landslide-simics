/**
 * @file mutexes.h
 * @brief state for modeling userspace mutex behaviour
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#ifndef __LS_MUTEXES_H
#define __LS_MUTEXES_H

#include "variable_queue.h"

struct ls_state;

struct mutex_state {
	int size;
};

void mutexes_init(struct mutex_state *m);

void check_user_mutex_access(struct ls_state *ls, unsigned int addr);

#endif
