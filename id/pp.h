/**
 * @file pp.h
 * @brief preemption points
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_PP_H
#define __ID_PP_H

#include <stdbool.h>

#define PRIORITY_NONE         ((unsigned int)0x00)
#define PRIORITY_DR_CONFIRMED ((unsigned int)0x01)
#define PRIORITY_DR_SUSPECTED ((unsigned int)0x02)
#define PRIORITY_MUTEX_LOCK   ((unsigned int)0x04)
#define PRIORITY_MUTEX_UNLOCK ((unsigned int)0x08)
#define PRIORITY_OTHER        ((unsigned int)0x10)
#define PRIORITY_ALL          ((unsigned int)~0x0)

struct pp {
	/* all read-only once created */
	char *config_str; /* e.g., "data_race 0xdeadbeef 0x47" */
	unsigned int priority;
	unsigned int id; /* global unique identifier among PPs */
	unsigned int generation;
};

struct pp_set {
	unsigned int len;
	bool array[0];
};

/* pp registry functions */
struct pp *pp_new(char *config_str, unsigned int priority,
		    unsigned int generation);
struct pp *pp_get(unsigned int id);

/* pp set manipulation functions */
struct pp_set *create_pp_set(unsigned int pp_mask);
void free_pp_set(struct pp_set *set);
struct pp *pp_next(struct pp_set *set, struct pp *current); /* for iteration */
unsigned int compute_generation(struct pp_set *set);

#define FOR_EACH_PP(pp, set)				\
	for (pp = pp_next((set), NULL); pp != NULL;	\
	     pp = pp_next((set), (pp)))

#endif
