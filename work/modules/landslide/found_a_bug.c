/**
 * @file found_a_bug.c
 * @brief function for dumping debug info and quitting simics when finding a bug
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <fcntl.h> /* for open */
#include <unistd.h> /* for write */

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

#define html_print_buf(fd, buf, len) do {			\
		unsigned int ret = write(fd, buf, len);		\
		assert(ret > 0 && "failed write");		\
	} while (0)

#define html_printf(fd, ...) do {					\
		const int buflen = 1024;				\
		char buf[buflen];					\
		unsigned int len = scnprintf(buf, buflen, __VA_ARGS__);	\
		assert(len > 0 && "failed scnprintf");			\
		html_print_buf(fd, buf, len);				\
	} while(0)

/* returns an open file descriptor */
static int begin_html_output(const char *filename) {
	int fd = open(filename, O_CREAT | O_WRONLY | O_APPEND,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	assert(fd != -1 && "failed open html file");

	html_printf(fd, "<html><head><title>\n");
	html_printf(fd, "landslide preemption trace output\n");
	html_printf(fd, "</title><style>\n");
	html_printf(fd, "table,th,td { border:1px solid black; }\n");
	html_printf(fd, "</style></head><body>\n");
	html_printf(fd, "<marquee>&iexcl;CUIDADO! &iexcl;LAS LLAMAS SON MUY PELIGROSAS!</marquee>\n");
	return fd;
}

static void end_html_output(int fd) {
	html_printf(fd, "</body>\n");
	html_printf(fd, "</html>\n");
	int ret = close(fd);
	assert(ret == 0 && "failed close");
}

#define MAX_TRACE_LEN 2048

static void html_print_stack_trace(int fd, struct stack_trace *st)
{
	char buf[MAX_TRACE_LEN];
	unsigned int length = html_stack_trace(buf, MAX_TRACE_LEN, st);
	html_print_buf(fd, buf, length);
}

static void html_print_stack_trace_in_table(int fd, table_column_map_t *m,
					    struct stack_trace *st)
{
	bool found = false;
	html_printf(fd, "<tr>");
	int i;
	unsigned int *tidp;
	ARRAY_LIST_FOREACH(m, i, tidp) {
		html_printf(fd, "<td>");
		if (*tidp == st->tid) {
			assert(!found && "duplicate tid in map");
			html_print_stack_trace(fd, st);
			found = true;
		}
		html_printf(fd, "</td>");
	}
	html_printf(fd, "</tr>\n");
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

/* html_fd and map are valid iff tabular is true */
static unsigned int print_tree_from(struct hax *h, unsigned int choose_thread,
				    bool bug_found, bool tabular,
				    unsigned int html_fd,
				    table_column_map_t *map,
				    bool verbose)
{
	unsigned int num;

	if (h == NULL) {
		assert(choose_thread == -1);
		return 0;
	}

	num = 1 + print_tree_from(h->parent, h->chosen_thread, bug_found,
				  tabular, html_fd, map, verbose);

	if (h->chosen_thread != choose_thread || verbose) {
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
			html_print_stack_trace_in_table(html_fd, map, h->stack_trace);
		}
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
		save_setjmp(&ls->save, ls, -1, true, true, false);
		unsigned int _tid;
		explore(&ls->save, &_tid);
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


void _found_a_bug(struct ls_state *ls, bool bug_found, bool verbose,
		  char *reason, unsigned int reason_len)
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
	int html_fd;
	table_column_map_t map;

	if (tabular) {
		/* Also print trace to html output file. */
		html_fd = begin_html_output(ls->html_file);

		if (bug_found) {
			html_printf(html_fd, HTML_COLOUR_START(HTML_COLOUR_RED)
				    "<h2>A bug was found!</h2>\n"
				    HTML_COLOUR_END);
			html_printf(html_fd, "Current stack:<br />\n");
			html_print_stack_trace(html_fd, stack);
		} else {
			html_printf(html_fd, HTML_COLOUR_START(HTML_COLOUR_BLUE)
				    "<h2>Preemption point info follows. "
				    "No bug was found.</h2><br />\n" HTML_COLOUR_END);
		}
		if (reason) {
			// FIXME: Sanitize reason; e.g. for deadlocks the message
			// will contain "->" which may not render properly.
			html_printf(html_fd, "%s<h3>%.*s</h3>\n" HTML_COLOUR_END,
				    bug_found ? HTML_COLOUR_START(HTML_COLOUR_RED)
				              : HTML_COLOUR_START(HTML_COLOUR_BLUE),
				    reason_len, reason);
		}
		html_printf(html_fd, "Total backtracks: %d<br />\n",
			    ls->save.total_jumps);
		html_printf(html_fd, "Estimated state space size: %Lf<br />\n",
			    (ls->save.total_jumps + 1) / proportion);
		html_printf(html_fd, "Estimated state space coverage: %Lf%%<br />\n",
			    proportion * 100);
		html_printf(html_fd, "<br />\n");

		/* Figure out how many columns the table will need. */
		init_table_column_map(&map, &ls->save, stack->tid);
		assert(ARRAY_LIST_SIZE(&map) > 0);

		html_printf(html_fd, "<table><tr>\n");
		int i;
		unsigned int *tidp;
		ARRAY_LIST_FOREACH(&map, i, tidp) {
			html_printf(html_fd, "<td><div style=\"%s\">",
				    "font-size:large;text-align:center");
			html_printf(html_fd, "TID %d", *tidp);
			if (*tidp == kern_get_init_tid()) {
				html_printf(html_fd, " (init)");
			} else if (*tidp == kern_get_shell_tid()) {
				html_printf(html_fd, " (shell)");
			} else if (kern_has_idle() && *tidp == kern_get_idle_tid()) {
				html_printf(html_fd, " (idle)");
			}
			html_printf(html_fd, "</div></td>\n");
		}
		html_printf(html_fd, "</tr>\n");
	}

	/* Walk current branch from root. */
	print_tree_from(ls->save.current, ls->save.next_tid, bug_found,
			tabular, html_fd, &map, verbose);

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
			html_print_stack_trace_in_table(html_fd, &map, stack);
		}
		html_printf(html_fd, "</table>\n");
		ARRAY_LIST_FREE(&map);
		end_html_output(html_fd);
		lsprintf(BUG, bug_found, COLOUR_BOLD COLOUR_GREEN
			 "Tabular preemption trace output to %s\n." COLOUR_DEFAULT,
			 ls->html_file);
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
