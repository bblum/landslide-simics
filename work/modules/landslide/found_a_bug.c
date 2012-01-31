/**
 * @file found_a_bug.c
 * @brief function for dumping debug info and quitting simics when finding a bug
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <simics/api.h>

#define MODULE_NAME "BUG!"
#define MODULE_COLOUR COLOUR_RED

#include "common.h"
#include "landslide.h"
#include "schedule.h"
#include "tree.h"

static int print_tree_from(struct hax *h, int choose_thread)
{
	int num;

	if (h == NULL) {
		assert(choose_thread == -1);
		return 0;
	}
	
	num = 1 + print_tree_from(h->parent, h->chosen_thread);
	lsprintf("%d:\teip 0x%.8x, tc %d, old %d new %d, ", num,
		 h->eip, h->trigger_count, h->chosen_thread, choose_thread);
	print_q("RQ [", &h->oldsched->rq, "]\n");

	return num;
}

void found_a_bug(struct ls_state *ls)
{
	lsprintf("****    A bug was found!   ****\n");
	lsprintf("**** Choice trace follows. ****\n");

	print_tree_from(ls->save.current, ls->save.next_tid);

	lsprintf("Current eip 0x%.8x, trigger_count %d, total triggers %d\n",
		 ls->eip, ls->trigger_count, ls->absolute_trigger_count);
	lsprintf("Total choices %d, points %d, total backtracks %d, depths %d\n",
		 ls->save.total_choices, ls->save.total_choice_poince,
		 ls->save.total_jumps, ls->save.depth_total);

	// FIXME: this should probably be SIM_break_simulation instead.
	SIM_quit(LS_BUG_FOUND);
}
