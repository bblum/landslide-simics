/**
 * @file found_a_bug.c
 * @brief function for dumping debug info and quitting simics when finding a bug
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <simics/api.h>

#define MODULE_NAME "BUG!"
#define MODULE_COLOUR COLOUR_RED

#define INFO_NAME "INFO"
#define INFO_COLOUR COLOUR_DARK COLOUR_GREEN

#include "common.h"
#include "found_a_bug.h"
#include "kernel_specifics.h"
#include "landslide.h"
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

/* bypass the lsprintf printing framework entirely */
#define print_html(...) do { \
		fprintf(stderr, COLOUR_BOLD COLOUR_CYAN __VA_ARGS__); /* FIXME */ \
		fprintf(stderr, COLOUR_DEFAULT); \
	} while (0)

// TODO: html filename
static void emit_html_header(int num_columns) {
	print_html("\\documentclass{article}\n"
		   "\\usepackage{fullpage}\n"
		   "\\begin{document}\n");

	print_html("\\begin{tabular}{|");
	assert(num_columns > 0);
	for (int i = 0; i < num_columns; i++) {
		print_html("l|");
	}
	print_html("}\n");
}

static void emit_html_footer() {
	print_html("\\end{tabular}\n");
	print_html("\\end{document}\n");
}

/******************************************************************************
 * Original flavour
 ******************************************************************************/

/* Prints a stack trace with newlines and tabs instead of ", "s. */
static void _print_stack_trace(verbosity v, bool bug_found, bool tabular,
			       const char *stack) {
	const char *current_frame = stack;
	bool first_frame = true;

	if (tabular) {
		print_html("\\begin{tabular}{l}\n");
		// FIXME: strip "TID X at" prelude
	}

	while (current_frame != NULL) {
		const char *next_frame =
			strstr(current_frame, STACK_TRACE_SEPARATOR);
		int frame_length = next_frame == NULL ?
			(int)strlen(current_frame) :
			(int)(next_frame - current_frame);

		if (tabular) {
			print_html("%.*s\\\\\n", frame_length, current_frame);
			// FIXME: truncate filename; probably need stack trace datatype
		} else {
			lsprintf(v, bug_found, "\t%s%.*s\n", first_frame ? "" : "\t",
				 frame_length, current_frame);
		}

		first_frame = false;
		if ((current_frame = next_frame) != NULL) {
			current_frame += strlen(STACK_TRACE_SEPARATOR);
		}
	}

	if (tabular) {
		print_html("\\end{tabular}\n");
	}
}

void print_stack_trace(verbosity v, bool bug_found, const char *stack) {
	_print_stack_trace(v, bug_found, false, stack);
}

static int print_tree_from(struct hax *h, int choose_thread, bool bug_found,
			   bool tabular, bool verbose)
{
	int num;

	if (h == NULL) {
		assert(choose_thread == -1);
		return 0;
	}
	
	num = 1 + print_tree_from(h->parent, h->chosen_thread, bug_found,
				  tabular, verbose);

	if (h->chosen_thread != choose_thread || verbose) {
		lsprintf(BUG, bug_found,
			 COLOUR_BOLD COLOUR_YELLOW "%d:\t", num);
		if (h->chosen_thread == -1) {
			printf(BUG, "<none> ");
		} else {
			printf(BUG, "TID %d ", h->chosen_thread);
		}
		printf(BUG, "--> TID %d;  " COLOUR_DARK COLOUR_YELLOW, choose_thread);
		if (MAX_VERBOSITY >= DEV) {
		       printf(BUG, "instr count = %lu; ", h->trigger_count);
		}
		print_scheduler_state(BUG, h->oldsched);
		printf(BUG, COLOUR_DEFAULT "\n");
		_print_stack_trace(BUG, bug_found, tabular, h->stack_trace);
	}

	return num;
}

void _found_a_bug(struct ls_state *ls, bool bug_found, bool verbose)
{
	/* Should we emit a "tabular" preemption trace using html, or
	 * default to the all-threads-in-one-column plaintext output? */
	bool tabular = TABULAR_TRACE != 0;

	if (bug_found) {
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_RED
			 "****     A bug was found!      ****\n");
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_RED
			 "**** Preemption trace follows. ****\n");
	} else {
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_GREEN
			 "These were the decision points (no bug was found):\n");
	}

	if (tabular) {
		emit_html_header(1);
	}

	print_tree_from(ls->save.current, ls->save.next_tid, bug_found,
			tabular, verbose);

	char *stack = stack_trace(ls);
	lsprintf(BUG, bug_found, COLOUR_BOLD "%sCurrent stack:\n"
		 COLOUR_DEFAULT, bug_found ? COLOUR_RED : COLOUR_GREEN);
	_print_stack_trace(BUG, bug_found, tabular, stack);
	MM_FREE(stack);

	PRINT_TREE_INFO(BUG, bug_found, ls);

	if (tabular) {
		emit_html_footer();
	}

	if (BREAK_ON_BUG) {
		lsprintf(ALWAYS, bug_found, COLOUR_BOLD COLOUR_YELLOW "%s", bug_found ?
			 "Now giving you the debug prompt. Good luck!\n" :
			 "Now giving you the debug prompt.\n");
		SIM_break_simulation(NULL);
	} else {
		SIM_quit(bug_found ? LS_BUG_FOUND : LS_NO_KNOWN_BUG);
	}
}
