/**
 * @file pp.c
 * @brief preemption poince
 * @author Ben Blum
 */

#include <stdio.h>  /* file io */
#include <unistd.h> /* unlink */

#include <simics/api.h>

#define MODULE_NAME "PP"

#include "common.h"
#include "kspec.h"
#include "landslide.h"
#include "pp.h"
#include "stack.h"
#include "student_specifics.h"

void pps_init(struct pp_config *p)
{
	p->dynamic_pps_loaded = false;
	ARRAY_LIST_INIT(&p->kern_withins, 16);
	ARRAY_LIST_INIT(&p->user_withins, 16);
	ARRAY_LIST_INIT(&p->data_races,   16);
	p->output_pipe_filename = NULL;
	p->input_pipe_filename  = NULL;

	/* Load PPs from static config (e.g. if not running under quicksand) */

	static const unsigned int kfuncs[][3] = KERN_WITHIN_FUNCTIONS;
	for (int i = 0; i < ARRAY_SIZE(kfuncs); i++) {
		struct pp_within pp = { .func_start = kfuncs[i][0],
		                        .func_end   = kfuncs[i][1],
		                        .within     = (kfuncs[i][2] != 0) };
		ARRAY_LIST_APPEND(&p->kern_withins, pp);
	}

	static const unsigned int ufuncs[][3] = USER_WITHIN_FUNCTIONS;
	for (int i = 0; i < ARRAY_SIZE(ufuncs); i++) {
		struct pp_within pp = { .func_start = ufuncs[i][0],
		                        .func_end   = ufuncs[i][1],
		                        .within     = (ufuncs[i][2] != 0) };
		ARRAY_LIST_APPEND(&p->user_withins, pp);
	}

	/* [i][0] is instruction pointer of the data race;
	 * [i][1] is the current TID when the race was observed;
	 * [i][2] is the last_call'ing eip value, if any;
	 * [i][3] is the most_recent_syscall when the race was observed. */
	static const unsigned int drs[][4] = DATA_RACE_INFO;
	for (int i = 0; i < ARRAY_SIZE(drs); i++) {
		struct pp_data_race pp = { .addr                = drs[i][0],
		                           .tid                 = drs[i][1],
		                           .last_call           = drs[i][2],
		                           .most_recent_syscall = drs[i][3] };
		ARRAY_LIST_APPEND(&p->data_races, pp);
	}
}

bool load_dynamic_pps(struct ls_state *ls, const char *filename)
{
	struct pp_config *p = &ls->pps;
	if (p->dynamic_pps_loaded) {
		return false;
	}

	lsprintf(DEV, "using dynamic PPs from %s\n", filename);
	FILE *pp_file = fopen(filename, "r");
	assert(pp_file != NULL && "failed open pp file");
	char buf[BUF_SIZE];
	while (fgets(buf, BUF_SIZE, pp_file) != NULL) {
		unsigned int x, y, z, w;
		int ret;
		if (buf[strlen(buf) - 1] == '\n') {
			buf[strlen(buf) - 1] = 0;
		}
		if (buf[0] == 'O') { /* capital letter o, not numeral 0 */
			/* expect filename to start immediately after a space */
			assert(buf[1] == ' ');
			assert(buf[2] != ' ' && buf[2] != '\0');
			assert(p->output_pipe_filename == NULL);
			p->output_pipe_filename = MM_XSTRDUP(buf + 2);
			lsprintf(DEV, "output %s\n", p->output_pipe_filename);
		} else if (buf[0] == 'I') {
			/* expect filename to start immediately after a space */
			assert(buf[1] == ' ');
			assert(buf[2] != ' ' && buf[2] != '\0');
			assert(p->input_pipe_filename == NULL);
			p->input_pipe_filename = MM_XSTRDUP(buf + 2);
			lsprintf(DEV, "input %s\n", p->input_pipe_filename);
		} else if ((ret = sscanf(buf, "K %x %x %x", &x, &y, &z)) != 0) {
			/* kernel within function directive */
			assert(ret == 3 && "invalid kernel within PP");
			lsprintf(DEV, "new PP: kernel %x %x %x\n", x, y, z);
			struct pp_within pp = { .func_start = x, .func_end = y,
			                        .within = (z != 0) };
			ARRAY_LIST_APPEND(&p->kern_withins, pp);
		} else if ((ret = sscanf(buf, "U %x %x %x", &x, &y, &z)) != 0) {
			/* user within function directive */
			assert(ret == 3 && "invalid user within PP");
			lsprintf(DEV, "new PP: user %x %x %x\n", x, y, z);
			struct pp_within pp = { .func_start = x, .func_end = y,
			                        .within = (z != 0) };
			ARRAY_LIST_APPEND(&p->user_withins, pp);
		} else if ((ret = sscanf(buf, "DR %x %x %x %x", &x, &y, &z, &w)) != 0) {
			/* data race preemption poince */
			assert(ret == 4 && "invalid data race PP");
			lsprintf(DEV, "new PP: dr %x %x %x %x\n", x, y, z, w);
			struct pp_data_race pp =
				{ .addr = x, .tid = y, .last_call = z,
				  .most_recent_syscall = w };
			ARRAY_LIST_APPEND(&p->data_races, pp);
		} else {
			/* unknown */
			lsprintf(DEV, "warning: unrecognized directive in "
				 "dynamic pp config file: '%s'\n", buf);
		}
	}
	fclose(pp_file);

	if (unlink(filename) < 0) {
		lsprintf(DEV, "warning: failed rm temp PP file %s\n", filename);
	}

	p->dynamic_pps_loaded = true;

	messaging_open_pipes(&ls->mess, p->input_pipe_filename,
			     p->output_pipe_filename);
	return true;
}

static bool check_withins(struct ls_state *ls, pp_within_list_t *pps)
{
	/* If there are no within_functions, the default answer is yes.
	 * Otherwise the default answer is no. Later ones take precedence, so
	 * all of them have to be compared. */
	bool any_withins = false;
	bool answer = true;
	unsigned int i;
	struct pp_within *pp;

	struct stack_trace *st = stack_trace(ls);

	ARRAY_LIST_FOREACH(pps, i, pp) {
		bool in = within_function_st(st, pp->func_start, pp->func_end);
		if (pp->within) {
			/* Switch to whitelist mode. */
			if (!any_withins) {
				any_withins = true;
				answer = false;
			}
			/* Must be within this function to allow. */
			if (in) {
				answer = true;
			}
		} else {
			/* Must NOT be within this function to allow. */
			if (in) {
				answer = false;
			}
		}
	}

	free_stack_trace(st);
	return answer;
}

bool kern_within_functions(struct ls_state *ls)
{
	return check_withins(ls, &ls->pps.kern_withins);
}

bool user_within_functions(struct ls_state *ls)
{
	return check_withins(ls, &ls->pps.user_withins);
}

bool suspected_data_race(struct ls_state *ls)
{
	struct pp_data_race *pp;
	unsigned int i;

#ifndef PINTOS_KERNEL
	// FIXME: Make this work for Pebbles kernel-space testing too.
	// Make the condition more precise (include testing_userspace() at least).
	if (!check_user_address_space(ls)) {
		return false;
	}
#endif

	ARRAY_LIST_FOREACH(&ls->pps.data_races, i, pp) {
		if (KERNEL_MEMORY(pp->addr)) {
#ifndef PINTOS_KERNEL
			assert(pp->most_recent_syscall != 0);
#endif
		} else {
			assert(pp->most_recent_syscall == 0);
		}

		if (pp->addr == ls->eip &&
		    (pp->tid == DR_TID_WILDCARD ||
		     pp->tid == ls->sched.cur_agent->tid) &&
		    (pp->last_call == 0 || /* last_call=0 -> anything */
		     pp->last_call == ls->sched.cur_agent->last_call) &&
		    pp->most_recent_syscall == ls->sched.cur_agent->most_recent_syscall) {
			return true;
		}
	}
	return false;
}

