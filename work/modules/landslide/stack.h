/**
 * @file stack.h
 * @brief stack tracing
 * @author Ben Blum
 */

#ifndef __LS_STACK_H
#define __LS_STACK_H

#include "common.h"
#include "variable_queue.h"

struct ls_state;

/* stack trace data structures. */
struct stack_frame {
	Q_NEW_LINK(struct stack_frame) nobe;
	unsigned int eip;
	char *name; /* may be null if symtable lookup failed */
	char *file; /* may be null, as above */
	int line;   /* valid iff above fields are not null */
};

Q_NEW_HEAD(struct stack_frames, struct stack_frame);

struct stack_trace {
	unsigned int tid;
	struct stack_frames frames;
};

/* interface */

/* utilities / glue */
void print_stack_frame(verbosity v, struct stack_frame *f);
void print_eip(verbosity v, unsigned int eip);
void print_stack_trace(verbosity v, struct stack_trace *st);
unsigned int html_stack_trace(char *buf, unsigned int maxlen, struct stack_trace *st);
struct stack_trace *copy_stack_trace(struct stack_trace *src);
void free_stack_trace(struct stack_trace *st);

/* actual logic */
struct stack_trace *stack_trace(struct ls_state *ls);
bool within_function(struct ls_state *ls, unsigned int func, unsigned int func_end);

/* convenience */
#define LS_ABORT() do { dump_stack(); assert(0); } while (0)
static inline void dump_stack() {
	struct stack_trace *st =
		stack_trace((struct ls_state *)SIM_get_object("landslide0"));
	lsprintf(ALWAYS, "Stack trace: ");
	print_stack_trace(ALWAYS, st);
	printf(ALWAYS, "\n");
	free_stack_trace(st);
}

#endif
