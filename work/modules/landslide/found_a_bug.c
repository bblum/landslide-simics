/**
 * @file found_a_bug.c
 * @brief function for dumping debug info and quitting simics when finding a bug
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <simics/api.h>

#define MODULE_NAME "BUG!"
#define MODULE_COLOUR COLOUR_RED

#include "common.h"
#include "found_a_bug.h"
#include "landslide.h"
#include "schedule.h"
#include "tree.h"
#include "x86.h"

// TODO: make this an ls_state attribute
#define VERBOSE_TRACE false

static int print_tree_from(struct hax *h, int choose_thread)
{
	int num;

	if (h == NULL) {
		assert(choose_thread == -1);
		return 0;
	}
	
	num = 1 + print_tree_from(h->parent, h->chosen_thread);

	if (h->chosen_thread != choose_thread || VERBOSE_TRACE) {
		lsprintf(BUG, COLOUR_BOLD COLOUR_YELLOW
			 "%d:\t%lu instructions, old %d new %d, ", num,
			 h->trigger_count, h->chosen_thread, choose_thread);
		print_qs(BUG, h->oldsched);
		printf(BUG, "\n");
		lsprintf(BUG, "\t%s\n", h->stack_trace);
	}

	return num;
}

void found_a_bug(struct ls_state *ls)
{
	lsprintf(BUG, COLOUR_BOLD COLOUR_RED
		 "****    A bug was found!     ****\n");
	lsprintf(BUG, COLOUR_BOLD COLOUR_RED
		 "**** Decision trace follows. ****\n");

	print_tree_from(ls->save.current, ls->save.next_tid);

	char *stack = stack_trace(ls->cpu0, ls->eip, ls->sched.cur_agent->tid);
	lsprintf(BUG, "Stack: %s\n", stack);
	MM_FREE(stack);

	PRINT_TREE_INFO(BUG, ls);

	// FIXME: this should probably be SIM_break_simulation instead.
	SIM_quit(LS_BUG_FOUND);
}
