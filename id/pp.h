/**
 * @file pp.h
 * @brief preemption points
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_PP_H
#define __ID_PP_H

#include <stdbool.h>

#define PRIORITY_DR_CONFIRMED 1
#define PRIORITY_DR_SUSPECTED 2
#define PRIORITY_MUTEX_LOCK   3
#define PRIORITY_MUTEX_UNLOCK 4
#define PRIORITY_OTHER        5

struct pp {
	/* all read-only once created */
	char *config_str; /* e.g., "data_race 0xdeadbeef 0x47" */
	unsigned int priority;
	unsigned int id; /* global unique identifier among PPs */
	unsigned int generation;
};

struct pp_set {
	unsigned int len;
	bool array[];
};

struct pp *pp_new(char *config_str, unsigned int priority,
		    unsigned int generation);
struct pp *pp_get(unsigned int id);

#define FOREACH_PP(pp, set)					\
	for (int __i = 0, pp = pp_get((set)->array[__i]);	\
	     __i < pp->len;					\
	     __i++, pp = (__i == (set)->len ? NULL : pp_get((set)->array[__i])))

#endif
