/**
 * @file pp.h
 * @brief preemption points
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_PP_H
#define __ID_PP_H

#include <stdbool.h>

#include "common.h"

/* numerically-lower priorities are more urgent */
#define PRIORITY_NONE         ((unsigned int)0x00)
#define PRIORITY_DR_CONFIRMED ((unsigned int)0x01)
#define PRIORITY_DR_SUSPECTED ((unsigned int)0x02)
#define PRIORITY_MUTEX_LOCK   ((unsigned int)0x04)
#define PRIORITY_MUTEX_UNLOCK ((unsigned int)0x08)
#define PRIORITY_CLI          ((unsigned int)0x10)
#define PRIORITY_STI          ((unsigned int)0x20)
#define PRIORITY_OTHER        ((unsigned int)0x40)
#define PRIORITY_ALL          ((unsigned int)~0x0)

struct pp {
	/* all read-only once created */
	char *config_str; /* e.g., "data_race 0xdeadbeef 0x47" */
	char *short_str;
	char *long_str;
	unsigned int priority;
	unsigned int id; /* global unique identifier among PPs */
	unsigned int generation;
	bool deterministic; /* for data race PPs */
	bool free_re_malloc;
	/* write-able, protected by global registry lock */
	bool explored; /* was a state space including this pp completed? */
};

struct pp_set {
	unsigned int size;
	unsigned int capacity;
	bool array[0];
};

/* pp registry functions */
struct pp *pp_new(char *config_str, char *short_str, char *long_str,
		  unsigned int priority, bool deterministic, bool free_re_malloc,
		  unsigned int generation, bool *duplicate);
struct pp *pp_get(unsigned int id);

void print_live_data_race_pps();
void try_print_live_data_race_pps(); /* signal handler safe; may do nothing. */

void print_free_re_malloc_false_positives();

/* pp set manipulation functions */
struct pp_set *create_pp_set(unsigned int pp_mask);
struct pp_set *clone_pp_set(struct pp_set *set);
struct pp_set *add_pp_to_set(struct pp_set *set, struct pp *pp);
void free_pp_set(struct pp_set *set);
void print_pp_set(struct pp_set *set, bool short_strs);
bool pp_set_equals(struct pp_set *x, struct pp_set *y);
bool pp_subset(struct pp_set *sub, struct pp_set *super);
struct pp *pp_next(struct pp_set *set, struct pp *current); /* for iteration */
bool pp_set_contains(struct pp_set *set, struct pp *pp);

unsigned int compute_generation(struct pp_set *set);
void record_explored_pps(struct pp_set *set);
struct pp_set *filter_unexplored_pps(struct pp_set *set);
unsigned int unexplored_priority(struct pp_set *set);

#define FOR_EACH_PP(pp, set)				\
	for (pp = pp_next((set), NULL); pp != NULL;	\
	     pp = pp_next((set), (pp)))

#define MAKE_DR_PP_STR(buf, maxlen, eip, tid, lc, mrs)			\
	scnprintf((buf), (maxlen), "data_race 0x%x 0x%x 0x%x 0x%x",	\
		  (eip), (tid), (lc), (mrs))

#define IS_DATA_RACE(priority) \
	((priority) == PRIORITY_DR_CONFIRMED || (priority) == PRIORITY_DR_SUSPECTED)

#endif
