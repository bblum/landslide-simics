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

/* FFS, it's 2014. In any civilized language this would be 0 lines of code. */
struct table_column_map {
	int *tids;
	/* if this research project lives long enough to see the day when
	 * computers routinely have 5 billion threads, i will have bigger
	 * problems than overflow. */
	unsigned int num_tids;
	unsigned int capacity;
};

#define FOREACH_TABLE_COLUMN(tid_var, m)		\
	for (int __col = 0, tid_var = (m)->tids[__col];	\
	     __col < (m)->num_tids;			\
	     __col++, tid_var = (m)->tids[__col])

static void add_table_column_tid(struct table_column_map *m, int tid)
{
	assert(m->num_tids <= m->capacity);
	if (m->num_tids == m->capacity) {
		int *old_array = m->tids;
		assert(m->capacity < UINT_MAX / 2);
		m->capacity *= 2;
		m->tids = MM_XMALLOC(m->capacity, typeof(*m->tids));
		memcpy(m->tids, old_array, m->num_tids * sizeof(*m->tids));
		MM_FREE(old_array);
	}
	m->tids[m->num_tids] = tid;
	m->num_tids++;
}

static void init_table_column_map(struct table_column_map *m, struct save_state *ss,
				  int current_tid)
{
	m->capacity = 128; /* whatever */
	m->num_tids = 0;
	m->tids = MM_XMALLOC(m->capacity, typeof(*m->tids));

	/* current tid may not show up in history. add as a special case. */
	add_table_column_tid(m, current_tid);
	for (struct hax *h = ss->current; h != NULL; h = h->parent) {
		/* add it if it's not already present */
		bool present = false;
		for (int i = 0; i < m->num_tids; i++) {
			if (m->tids[i] == h->stack_trace->tid) {
				present = true;
				break;
			}
		}
		if (!present) {
			add_table_column_tid(m, h->stack_trace->tid);
		}
	}

	/* sort for user friendliness */
	for (int i = 0; i < m->num_tids; i++) {
		for (int j = i + 1; j < m->num_tids; j++) {
			if (m->tids[j] < m->tids[i]) {
				int tmp = m->tids[j];
				m->tids[j] = m->tids[i];
				m->tids[i] = tmp;
			}
		}
	}
}

#define clear_table_column_map(m) MM_FREE((m)->tids)

/******************** actual logic ********************/

#define html_print_buf(fd, buf, len) do {		\
		int ret = write(fd, buf, len);		\
		assert(ret > 0 && "failed write");	\
	} while (0)

#define html_printf(fd, ...) do {				\
		const int buflen = 1024;			\
		char buf[buflen];				\
		int len = snprintf(buf, buflen, __VA_ARGS__);	\
		assert(len > 0 && "failed snprintf");		\
		html_print_buf(fd, buf, len);			\
	} while(0)

/* returns an open file descriptor */
static int begin_html_output(const char *filename, int num_columns) {
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

static void html_print_stack_trace(int fd, struct table_column_map *m,
				   struct stack_trace *st)
{
	bool found = false;
	html_printf(fd, "<tr>");
	for (int i = 0; i < m->num_tids; i++) {
		html_printf(fd, "<td>");
		if (m->tids[i] == st->tid) {
			/* found appropriate column. render stack trace in
			 * html and spit it out into this table cell. */
			char buf[MAX_TRACE_LEN];
			int length = html_stack_trace(buf, MAX_TRACE_LEN, st);
			html_print_buf(fd, buf, length);
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
static int print_tree_from(struct hax *h, int choose_thread, bool bug_found,
			   bool tabular, int html_fd, struct table_column_map *map,
			   bool verbose)
{
	int num;

	if (h == NULL) {
		assert(choose_thread == -1);
		return 0;
	}

	num = 1 + print_tree_from(h->parent, h->chosen_thread, bug_found,
				  tabular, html_fd, map, verbose);

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
		/* print stack trace, either in html or console format */
		print_stack_to_console(h->stack_trace, bug_found, "\t");
		if (tabular) {
			html_print_stack_trace(html_fd, map, h->stack_trace);
		}
	}

	return num;
}

void _found_a_bug(struct ls_state *ls, bool bug_found, bool verbose,
		  char *reason, int reason_len)
{
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
			 "These were the decision points (no bug was found):\n");
	}

	struct stack_trace *stack = stack_trace(ls);
	int html_fd;
	struct table_column_map map;

	if (tabular) {
		/* Also print trace to html output file. */
		html_fd = begin_html_output(ls->html_file, map.num_tids);

		if (bug_found) {
			html_printf(html_fd, HTML_COLOUR_START(HTML_COLOUR_RED)
				    "<h2>A bug was found!</h2><br />\n"
				    HTML_COLOUR_END);
		} else {
			html_printf(html_fd, HTML_COLOUR_START(HTML_COLOUR_BLUE)
				    "<h2>Preemption point info follows. "
				    "No bug was found.</h2><br />\n" HTML_COLOUR_END);
		}
		if (reason) {
			// FIXME: Sanitize reason; e.g. for deadlocks the message
			// will contain "->" which may not render properly.
			html_printf(html_fd, "%s<h3>%.*s</h3><br />\n" HTML_COLOUR_END,
				    bug_found ? HTML_COLOUR_START(HTML_COLOUR_RED)
				              : HTML_COLOUR_START(HTML_COLOUR_BLUE),
				    reason_len, reason);
		}
		html_printf(html_fd, "Total backtracks: %d<br /><br />\n",
			    ls->save.total_jumps);

		/* Figure out how many columns the table will need. */
		init_table_column_map(&map, &ls->save, stack->tid);
		assert(map.num_tids > 0);

		html_printf(html_fd, "<table><tr>\n");
		int MAYBE_UNUSED /* wtf, gcc? */ tid;
		FOREACH_TABLE_COLUMN(tid, &map) {
			html_printf(html_fd, "<td><div style=\"%s\">",
				    "font-size:large;text-align:center");
			html_printf(html_fd, "TID %d</div></td>\n", tid);
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

	if (tabular) {
		/* Finish up html output */
		html_print_stack_trace(html_fd, &map, stack);
		html_printf(html_fd, "</table>\n");
		clear_table_column_map(&map);
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
