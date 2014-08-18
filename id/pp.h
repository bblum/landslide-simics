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

struct pp {
	char *config_str; /* e.g., "data_race 0xdeadbeef 0x47" */
	unsigned int priority;
	unsigned int id; /* global unique identifier among PPs */
};

struct pp_set {
	int len;
	bool *array;
};

unsigned int pp_new(char *config_str, unsigned int priority);
void pp_get(unsigned int id, char **config_str, unsigned int *priority);

#endif
