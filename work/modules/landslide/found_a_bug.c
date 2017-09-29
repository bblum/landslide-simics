/**
 * @file found_a_bug.c
 * @brief function for dumping debug info and quitting simics when finding a bug
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <fcntl.h> /* for open */

#include <simics/api.h>

#define MODULE_NAME "BUG!"
#define MODULE_COLOUR COLOUR_RED

#define INFO_NAME "INFO"
#define INFO_COLOUR COLOUR_DARK COLOUR_GREEN

#include "common.h"
#include "explore.h"
#include "found_a_bug.h"
#include "html.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "messaging.h"
#include "schedule.h"
#include "stack.h"
#include "tree.h"

/* The default print macros would print big red "[BUG!]"s even if we're just
 * dumping decision info. Redefine them to be flexible around this point. */
#undef lsprintf
#define lsprintf(v, bug_found, ...) do {				\
	if (bug_found) {						\
		_lsprintf(v, MODULE_NAME, MODULE_COLOUR, __VA_ARGS__);	\
	} else {							\
		_lsprintf(v, INFO_NAME, INFO_COLOUR, __VA_ARGS__);	\
	} } while (0)

#undef PRINT_TREE_INFO
#define PRINT_TREE_INFO(v, bug_found, ls) do {				\
	if (bug_found) {						\
		_PRINT_TREE_INFO(v, MODULE_NAME, MODULE_COLOUR, ls);	\
	} else {							\
		_PRINT_TREE_INFO(v, INFO_NAME, INFO_COLOUR, ls);	\
	} } while (0)

/******************************************************************************
 * Helpers for printing a tabular trace in html
 ******************************************************************************/

/******************** gross glue ********************/

typedef ARRAY_LIST(unsigned int) table_column_map_t;

static void init_table_column_map(table_column_map_t *m, struct save_state *ss,
				  int current_tid)
{
	ARRAY_LIST_INIT(m, 64);

	/* current tid may not show up in history. add as a special case. */
	ARRAY_LIST_APPEND(m, current_tid);
	for (struct hax *h = ss->current; h != NULL; h = h->parent) {
		/* add it if it's not already present */
		bool present = false;
		int i;
		unsigned int *tidp;
		ARRAY_LIST_FOREACH(m, i, tidp) {
			if (*tidp == h->stack_trace->tid) {
				present = true;
				break;
			}
		}
		if (!present) {
			ARRAY_LIST_APPEND(m, h->stack_trace->tid);
		}
	}

	/* sort for user friendliness */
	for (int i = 0; i < ARRAY_LIST_SIZE(m); i++) {
		for (int j = i + 1; j < ARRAY_LIST_SIZE(m); j++) {
			if (*ARRAY_LIST_GET(m, j) < *ARRAY_LIST_GET(m, i)) {
				ARRAY_LIST_SWAP(m, i, j);
			}
		}
	}
}

/******************** actual logic ********************/

/* returns an open file descriptor */
static void begin_html_output(const char *filename, struct fab_html_env *env) {
	env->html_fd = open(filename, O_CREAT | O_WRONLY | O_APPEND,
			    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	assert(env->html_fd != -1 && "failed open html file");

	HTML_PRINTF(env, "<html><head><title>\n");
	HTML_PRINTF(env, "landslide preemption trace output\n");
	HTML_PRINTF(env, "</title><style>\n");
	HTML_PRINTF(env, "table,th,td { border:1px solid black; }\n");
	HTML_PRINTF(env, "</style></head><body>\n");
	//HTML_PRINTF(env, "<marquee>&iexcl;CUIDADO! &iexcl;LAS LLAMAS SON MUY PELIGROSAS!</marquee>\n");
}

static void end_html_output(struct fab_html_env *env) {
	HTML_PRINTF(env, "</body>\n");
	HTML_PRINTF(env, "</html>\n");
	int ret = close(env->html_fd);
	assert(ret == 0 && "failed close");
}

static void html_print_stack_trace_in_table(struct fab_html_env *env,
					    table_column_map_t *m,
					    struct stack_trace *st)
{
	bool found = false;
	HTML_PRINTF(env, "<tr>");
	int i;
	unsigned int *tidp;
	ARRAY_LIST_FOREACH(m, i, tidp) {
		HTML_PRINTF(env, "<td>");
		if (*tidp == st->tid) {
			assert(!found && "duplicate tid in map");
			HTML_PRINT_STACK_TRACE(env, st);
			found = true;
		}
		HTML_PRINTF(env, "</td>");
	}
	HTML_PRINTF(env, "</tr>\n");
	assert(found && "tid missing in table column map");
}

/******************************************************************************
 * Original flavour
 ******************************************************************************/

/* As print_stack in stack.c, but prints directly to console, on multiple lines
 * with tabs for alignment, rather than on one line with comma separators. */
void print_stack_to_console(struct stack_trace *st, bool bug_found, const char *prefix)
{
	struct stack_frame *f;
	bool first_frame = true;

	/* print TID prefix before first frame */
	lsprintf(BUG, bug_found, "%sTID%d at ", prefix, st->tid);

	/* print each frame */
	Q_FOREACH(f, &st->frames, nobe) {
		if (!first_frame) {
			printf(BUG, "\n");
			lsprintf(BUG, bug_found, "%s\t", prefix);
		}
		first_frame = false;
		print_stack_frame(BUG, f);
	}

	printf(BUG, "\n");
}

/* env and map are valid iff tabular is true */
static unsigned int print_tree_from(struct hax *h, unsigned int choose_thread,
				    bool bug_found, bool tabular,
				    struct fab_html_env *env,
				    table_column_map_t *map,
				    bool verbose, unsigned int *trace_length)
{
	unsigned int num;

	if (h == NULL) {
		assert(choose_thread == -1);
		return 0;
	}

	num = 1 + print_tree_from(h->parent, h->chosen_thread, bug_found,
				  tabular, env, map, verbose, trace_length);

	if (h->is_preemption_point &&
	    (h->chosen_thread != choose_thread || verbose)) {
		lsprintf(BUG, bug_found,
			 COLOUR_BOLD COLOUR_YELLOW "%u:\t", num);
		if (h->chosen_thread == -1) {
			printf(BUG, "<none> ");
		} else {
			printf(BUG, "TID %d ", h->chosen_thread);
		}
		if (choose_thread == -1) {
			printf(BUG, "--> <none>;  ");
		} else {
			printf(BUG, "--> TID %d;  ", choose_thread);
		}
		printf(BUG, COLOUR_DARK COLOUR_YELLOW);
		if (MAX_VERBOSITY >= DEV) {
		       printf(BUG, "instr count = %lu; ", h->trigger_count);
		}
		print_scheduler_state(BUG, h->oldsched);
		printf(BUG, COLOUR_DEFAULT "\n");
		/* print stack trace, either in html or console format */
		print_stack_to_console(h->stack_trace, bug_found, "\t");
		if (tabular) {
			html_print_stack_trace_in_table(env, map, h->stack_trace);
		}
		*trace_length = *trace_length + 1;
	}

	return num;
}

/* ensure that a state space estimate has been computed, if it has not already,
 * and adjust for whether we aborted this branch early because we found a bug. */
// XXX: There's not really any one good place to put this function.
// FIXME: This will be an important puzzle piece when it's time to make
// landslide able to continue exploration after finding_a_bug. Will need to
// coalesce the logic here with the logic in landslide.c to do time travel.
static long double compute_state_space_size(struct ls_state *ls,
					    bool *needed_compute /* XXX hack */)
{
	if (ls->save.root == NULL) {
		lsprintf(DEV, "Warning: FAB before 1st PP established. "
			 "Can't estimate state space size.\n");
		*needed_compute = false;
		return 1.0L;
	}

	/* How can our estimate be accurate when we are possibly aborting this
	 * branch early due to a bug? If we run the estimation algorithm with
	 * the current state as the leaf nobe, it will incorrectly assume that
	 * sibling subtrees are "truncated" the same way this branch was.
	 *
	 * Instead, we'll just use whatever value the last estimation computed
	 * for the size of the "lost" subtree below this point. The exception is
	 * when we're on the 1st branch (either dumping preemption info, or
	 * foundabug deterministically), in which case we don't even know how
	 * long the test execution is supposed to run, so doing the estimation
	 * after all is the best we can do. */
	if (ls->save.total_jumps == 0) {
		/* First branch - either found a 'deterministic' bug, or
		 * asked to output PP info after branch completion. Either way,
		 * need to add a terminal 'leaf' nobe before computing the
		 * estimate. (See landslide.c:check_test_state().)
		 *
		 * Note that this is compatible with the assertion in save.c
		 * that non-leaf nobes don't get estimated upon, because we are
		 * adding the leaf nobe here. */
		// FIXME: Figure out whether "estimate the first branch" will
		// screw up future estimations if we keep exploring, and if we
		// shouldn't instead just output "estimate unknown" or somehow
		// reverse it in that case.
		assert(ls->save.root->proportion == 0.0L);
		bool voluntary = ls->save.next_tid != 1 &&
		                 ls->save.next_tid != ls->sched.cur_agent->tid;
		ls->user_sync.yield_progress = NOTHING_INTERESTING;
		// XXX: Gross hack. If arbiter FAB deadlock on this branch, it
		// will call setjmp on its own. Avoid double call in that case.
		if (!voluntary || ls->sched.voluntary_resched_stack != NULL) {
			save_setjmp(&ls->save, ls, -1, true, true, true,
				    -1, voluntary, false);
		}
		unsigned int _tid;
		bool _txn;
		unsigned int _xabort_code;
		explore(ls, &_tid, &_txn, &_xabort_code);
		*needed_compute = true;
		return estimate_proportion(ls->save.root, ls->save.current);
	} else {
		// FIXME: Is "don't estimate this branch" sustainable in the
		// sense that if we explore more, future estimations will DTRT?
		assert(ls->save.root->proportion != 0.0L);
		assert(ls->save.root->proportion != -0.0L);
		*needed_compute = false;
		/* Modify the proportion to account for this 1 branch that we
		 * explored since. (Matters more with small state spaces.) */
		return ((ls->save.total_jumps + 1.0L) / ls->save.total_jumps)
			* ls->save.root->proportion;
	}
}

static unsigned int count_distinct_user_threads(table_column_map_t *map)
{
	int i;
	unsigned int count = 0;
	unsigned int *tidp;
	ARRAY_LIST_FOREACH(map, i, tidp) {
		if (!(TID_IS_INIT(*tidp) || TID_IS_SHELL(*tidp) ||
		      TID_IS_IDLE(*tidp))) {
			count++;
		}
	}
	return count;
}

void _found_a_bug(struct ls_state *ls, bool bug_found, bool verbose,
		  const char *reason, unsigned int reason_len, fab_cb_t callback)
{
	bool needed_compute_estimate; /* XXX hack */
	long double proportion = compute_state_space_size(ls, &needed_compute_estimate);

	/* Should we emit a "tabular" preemption trace using html, or
	 * default to the all-threads-in-one-column plaintext output? */
	bool tabular = TABULAR_TRACE != 0;

	if (reason) {
		lsprintf(BUG, bug_found, COLOUR_BOLD "%s%.*s\n" COLOUR_DEFAULT,
			 bug_found ? COLOUR_RED : COLOUR_GREEN, reason_len, reason);
	}

	if (bug_found) {
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_RED
			 "****     A bug was found!      ****\n");
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_RED
			 "**** Preemption trace follows. ****\n");
	} else {
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_GREEN
			 "These were the preemption points (no bug was found):\n");
	}

	struct stack_trace *stack = stack_trace(ls);
	struct fab_html_env env;
	table_column_map_t map;

	if (tabular) {
		/* Also print trace to html output file. */
		begin_html_output(ls->html_file, &env);

		if (bug_found) {
			HTML_PRINTF(&env, HTML_COLOUR_START(HTML_COLOUR_RED)
				    "<h2>A bug was found!</h2>\n"
				    HTML_COLOUR_END);
			HTML_PRINTF(&env, "Current stack (TID %u):" HTML_NEWLINE,
				    stack->tid);
			HTML_PRINT_STACK_TRACE(&env, stack);
		} else {
			HTML_PRINTF(&env, HTML_COLOUR_START(HTML_COLOUR_BLUE)
				    "<h2>Preemption point info follows. "
				    "No bug was found.</h2>"
				    HTML_NEWLINE HTML_COLOUR_END);
		}
		if (reason) {
			// FIXME: Sanitize reason; e.g. for deadlocks the message
			// will contain "->" which may not render properly.
			HTML_PRINTF(&env, "%s<h3>%.*s</h3>\n" HTML_COLOUR_END,
				    bug_found ? HTML_COLOUR_START(HTML_COLOUR_RED)
				              : HTML_COLOUR_START(HTML_COLOUR_BLUE),
				    reason_len, reason);
		}
		if (callback) {
			callback(&env);
			HTML_PRINTF(&env, HTML_NEWLINE);
		}
		HTML_PRINTF(&env, "Distinct interleavings tested: %" PRIu64
			    HTML_NEWLINE, ls->save.total_jumps + 1);
		HTML_PRINTF(&env, "Estimated state space size: %Lf" HTML_NEWLINE,
			    (ls->save.total_jumps + 1) / proportion);
		HTML_PRINTF(&env, "Estimated state space coverage: %Lf%%"
			    HTML_NEWLINE, proportion * 100);
		HTML_PRINTF(&env, HTML_NEWLINE);

		/* Figure out how many columns the table will need. */
		init_table_column_map(&map, &ls->save, stack->tid);
		assert(ARRAY_LIST_SIZE(&map) > 0);

		if (testing_userspace() && count_distinct_user_threads(&map) < 2) {
			HTML_PRINTF(&env, HTML_BOX_BEGIN "\n");
			HTML_PRINTF(&env, "%s<b>NOTE</b>:%s This bug was detected "
				    "before multiple user threads were created."
				    HTML_NEWLINE
				    "This is NOT A RACE, but more likely a problem "
				    "with your setup/initialization code.",
				    HTML_COLOUR_START(HTML_COLOUR_RED),
				    HTML_COLOUR_END);
			HTML_PRINTF(&env, HTML_BOX_END HTML_NEWLINE HTML_NEWLINE);
		}

		/* Print the tabular trace. */
		HTML_PRINTF(&env, "<table><tr>\n");
		int i;
		unsigned int *tidp;
		ARRAY_LIST_FOREACH(&map, i, tidp) {
			HTML_PRINTF(&env, "<td><div style=\"%s\">",
				    "font-size:large;text-align:center");
			HTML_PRINTF(&env, "TID %d", *tidp);
			if (TID_IS_INIT(*tidp)) {
				HTML_PRINTF(&env, " (init)");
			} else if (TID_IS_SHELL(*tidp)) {
				HTML_PRINTF(&env, " (shell)");
			} else if (TID_IS_IDLE(*tidp)) {
				HTML_PRINTF(&env, " (idle)");
			}
			HTML_PRINTF(&env, "</div></td>\n");
		}
		HTML_PRINTF(&env, "</tr>\n");
	}

	/* Walk current branch from root. */
	unsigned int trace_length = 0;
	print_tree_from(ls->save.current, ls->save.next_tid, bug_found,
			tabular, &env, &map, verbose, &trace_length);

	lsprintf(BUG, bug_found, COLOUR_BOLD "%sCurrent stack:\n"
		 COLOUR_DEFAULT, bug_found ? COLOUR_RED : COLOUR_GREEN);
	print_stack_to_console(stack, bug_found, "");

	PRINT_TREE_INFO(BUG, bug_found, ls);

	lsprintf(BUG, bug_found, "Estimated state space size: %Lf; coverage: %Lf%%\n",
		 (ls->save.total_jumps + 1) / proportion, proportion * 100);

	if (tabular) {
		/* Finish up html output */
		if (!needed_compute_estimate) {
			/* XXX hack -- suppress printing duplicate preemption
			 * point if a dummy preemption point was added for
			 * the purpose of computing a state space estimate. */
			html_print_stack_trace_in_table(&env, &map, stack);
		}
		HTML_PRINTF(&env, "</table>\n");
		ARRAY_LIST_FREE(&map);
		end_html_output(&env);
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_GREEN
			 "Tabular preemption trace output to %s\n." COLOUR_DEFAULT,
			 ls->html_file);
		if (bug_found) {
			message_found_a_bug(&ls->mess, ls->html_file, trace_length,
					    ls->sched.icb_preemption_count);
		}
	}
	MM_FREE(stack);

	if (BREAK_ON_BUG) {
		lsprintf(ALWAYS, bug_found, COLOUR_BOLD COLOUR_YELLOW "%s", bug_found ?
			 "Now giving you the debug prompt. Good luck!\n" :
			 "Now giving you the debug prompt.\n");
		SIM_break_simulation(NULL);
	} else {
		SIM_quit(bug_found ? LS_BUG_FOUND : LS_NO_KNOWN_BUG);
	}
}
