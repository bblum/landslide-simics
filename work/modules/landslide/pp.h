/**
 * @file pp.h
 * @brief preemption point config
 * @author Ben Blum
 */

#ifndef __LS_PP_H
#define __LS_PP_H

#include <simics/api.h>

#include "array_list.h"
#include "student_specifics.h"

struct ls_state;

/* ofc, these don't correspond 1-to-1 to PPs; they indicate whitelist/blacklist
 * directives that the arbiter should use to enable or disable mutex/etc PPs. */
struct pp_within {
	unsigned int func_start;
	unsigned int func_end;
	bool within;
};

/* these are more 1-to-1 */
struct pp_data_race {
	unsigned int addr;
	unsigned int tid;
	unsigned int last_call;
	unsigned int most_recent_syscall;
};

typedef ARRAY_LIST(struct pp_within) pp_within_list_t;

struct pp_config {
	bool dynamic_pps_loaded;
	pp_within_list_t kern_withins;
	pp_within_list_t user_withins;
	ARRAY_LIST(struct pp_data_race) data_races;
	char *output_pipe_filename;
	char *input_pipe_filename;
};

void pps_init(struct pp_config *p);
bool load_dynamic_pps(struct ls_state *ls, const char *filename);

bool kern_within_functions(struct ls_state *ls);
bool user_within_functions(struct ls_state *ls);
bool suspected_data_race(struct ls_state *ls);
#ifdef PREEMPT_EVERYWHERE
void maybe_preempt_here(struct ls_state *ls, unsigned int addr);
#endif

#endif
