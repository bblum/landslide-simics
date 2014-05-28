/**
 * @file stack.c
 * @brief stack tracing
 * @author Ben Blum
 */

#include <assert.h>

#define MODULE_NAME "STACK"
#define MODULE_COLOUR COLOUR_DARK COLOUR_BLUE

#include "common.h"
#include "landslide.h"
#include "kernel_specifics.h"
#include "stack.h"
#include "symtable.h"
#include "variable_queue.h"
#include "x86.h"

/* Performs a stack trace to see if the current call stack has the given
 * function somewhere on it. */
// FIXME: make this as intelligent as stack_trace.
// TODO: convert it to just call stack trace and look inside.
bool within_function(conf_object_t *cpu, int eip, int func, int func_end)
{
	if (eip >= func && eip < func_end)
		return true;

	bool in_userland = eip >= USER_MEM_START;

	eip = READ_STACK(cpu, 0);

	if (eip >= func && eip < func_end)
		return true;

	int stop_ebp = 0;
	int ebp = GET_CPU_ATTR(cpu, ebp);
	int rabbit = ebp;
	int frame_count = 0;

	while (ebp != stop_ebp && (in_userland || (unsigned)ebp < USER_MEM_START)
	       && frame_count++ < 1024) {
		/* Test eip against given range. */
		eip = READ_MEMORY(cpu, ebp + WORD_SIZE);
		if (eip >= func && eip < func_end)
			return true;

		/* Advance ebp and rabbit. Rabbit must go first to set stop_ebp
		 * accurately. */
		// XXX XXX XXX Actually fix the cycle detection - read from
		// rabbit not ebp; and get rid of the frame counter.
		// Fix this same bug in stack trace function below.
		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, ebp);
		if (rabbit == ebp) stop_ebp = ebp;
		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, ebp);
		if (rabbit == ebp) stop_ebp = ebp;
		ebp = READ_MEMORY(cpu, ebp);
	}

	return false;
}

#define MAX_TRACE_LEN 4096
#define ADD_STR(buf, pos, maxlen, ...) \
	do { pos += snprintf(buf + pos, maxlen - pos, __VA_ARGS__); } while (0)
#define ADD_FRAME(buf, pos, maxlen, eip, unknown) do {			\
	if (eip == kern_get_timer_wrap_begin()) {			\
		pos += snprintf(buf + pos, maxlen - pos, "<timer_wrapper>"); \
	} else {							\
		pos += symtable_lookup(buf + pos, maxlen - pos, eip, unknown);	\
	}								\
	} while (0)

/* Suppress stack frames from userspace, if testing userland, unless the
 * verbosity setting is high enough. */
#define SUPPRESS_FRAME(eip) \
	(MAX_VERBOSITY < DEV && testing_userspace() && \
	 (unsigned int)(eip) < USER_MEM_START)

/* Emits a "0xADDR in NAME (FILE:LINE)" line with pretty colours. */
void print_stack_frame(verbosity v, struct stack_frame *f)
{
	printf(v, "0x%.8x in ", f->eip);
	if (f->name == NULL) {
		printf(v, UNKNOWN_COLOUR "<unknown in %s>" COLOUR_DEFAULT,
		       f->eip < USER_MEM_START ? "kernel" : "user");
	} else {
		printf(v, FUNCTION_COLOUR "%s " FUNCTION_INFO_COLOUR, f->name);
		if (f->file == NULL) {
			printf(v, "<unknown assembly>");
		} else {
			printf(v, "(%s:%d)", f->file, f->line);
		}
		printf(v, COLOUR_DEFAULT);
	}
}

/* Prints a stack trace to the console. Uses printf, not lsprintf, separates
 * frames with ", ", and does not emit a newline at the end. */
void print_stack_trace(verbosity v, struct stack_trace *st)
{
	struct stack_frame *f;
	bool first_frame = true;

	/* print TID prefix before first frame */
	printf(v, "TID%d at ", st->tid);

	/* print each frame */
	Q_FOREACH(f, &st->frames, nobe) {
		if (!first_frame) {
			printf(v, ", ");
		}
		first_frame = false;
		print_stack_frame(v, f);
	}
}

/* Prints a stack trace to a multiline html table. Returns a malloced string. */
char *html_stack_trace(struct stack_trace *st)
{
	// TODO
	assert(0);
	return NULL;
}

struct stack_trace *copy_stack_trace(struct stack_trace *src)
{
	struct stack_trace *dest = MM_XMALLOC(1, struct stack_trace);
	struct stack_frame *f_src;
	dest->tid = src->tid;
	Q_INIT_HEAD(&dest->frames);

	Q_FOREACH(f_src, &src->frames, nobe) {
		struct stack_frame *f_dest = MM_XMALLOC(1, struct stack_frame);
		f_dest->eip = f_src->eip;
		f_dest->name = f_src->name == NULL ? NULL : MM_XSTRDUP(f_src->name);
		f_dest->file = f_src->file == NULL ? NULL : MM_XSTRDUP(f_src->file);
		f_dest->line = f_src->line;
		Q_INSERT_TAIL(&dest->frames, f_dest, nobe);
	}
	return dest;
}

void free_stack_trace(struct stack_trace *st)
{
	while (Q_GET_SIZE(&st->frames) > 0) {
		struct stack_frame *f = Q_GET_HEAD(&st->frames);
		Q_REMOVE(&st->frames, f, nobe);
		if (f->name != NULL) {
			MM_FREE(f->name);
		}
		if (f->file != NULL) {
			MM_FREE(f->file);
		}
		MM_FREE(f);
	}
	MM_FREE(st);
}

/* returns false if symtable lookup failed */
static bool add_frame(struct ls_state *ls, struct stack_trace *st, int eip)
{
	struct stack_frame *f = MM_XMALLOC(1, struct stack_frame);
	f->eip = eip;
	f->name = NULL;
	f->file = NULL;
	bool success = _symtable_lookup(eip, &f->name, &f->file, &f->line);
	Q_INSERT_TAIL(&st->frames, f, nobe);
	return success;
}

struct stack_trace *stack_trace(struct ls_state *ls)
{
	conf_object_t *cpu = ls->cpu0;
	int eip = ls->eip;
	int tid = ls->sched.cur_agent->tid;

	int stack_ptr = GET_CPU_ATTR(cpu, esp);

	struct stack_trace *st = MM_XMALLOC(1, struct stack_trace);
	st->tid = tid;
	Q_INIT_HEAD(&st->frames);

	/* Add current frame, even if it's in kernel and we're in user. */
	add_frame(ls, st, eip);

	int stop_ebp = 0;
	int ebp = GET_CPU_ATTR(cpu, ebp);
	int rabbit = ebp;
	int frame_count = 0;

	/* Figure out if the thread is vanishing and we should expect its cr3
	 * to have been freed (so we can't trace in userspace).
	 * (Note this condition will also hit init/shell, but we should not
	 * expect unknown symtable results from them in any case.) */
	bool wrong_cr3 =
		testing_userspace() && GET_CPU_ATTR(cpu, cr3) != ls->user_mem.cr3;

	while (ebp != 0 && (testing_userspace() || (unsigned)ebp < USER_MEM_START)
	       && frame_count++ < 1024) {
		bool extra_frame;

		/* This is the same check as at the end (after ebp advances),
		 * but duplicated here for the corner case where ebp's initial
		 * value is trash. (It's inside the loop so we can still look
		 * for extra frames.) */
		if ((unsigned int)ebp < GUEST_DATA_START) {
			ebp = 0;
		}

		/* Find "extra frames" of functions that don't set up an ebp
		 * frame or of untimely interrupts, before following ebp. */
		do {
			int eip_offset;
			bool iret_block = false;

			extra_frame = false;
			/* at the beginning or end of a function, there is no
			 * frame, but a return address is still on the stack. */
			if (function_eip_offset(eip, &eip_offset)) {
				if (eip_offset == 0) {
					extra_frame = true;
				} else if (eip_offset == 1 &&
				           READ_BYTE(cpu, eip - 1)
				           == OPCODE_PUSH_EBP) {
					stack_ptr += WORD_SIZE;
					extra_frame = true;
				}
			}
			if (!extra_frame) {
				/* Attempt to understand the tail end of syscall
				 * or interrupt wrappers. Traverse pushed GPRs
				 * if necessary to find the ret or iret. */
				int opcode;
				int opcode_offset = 0;
				do {
					opcode = READ_BYTE(cpu, eip + opcode_offset);
					opcode_offset++;
					if (opcode == OPCODE_RET) {
						extra_frame = true;
					} else if (opcode == OPCODE_IRET) {
						iret_block = true;
						extra_frame = true;
					} else if (OPCODE_IS_POP_GPR(opcode)) {
						stack_ptr += WORD_SIZE;
					} else if (opcode == OPCODE_POPA) {
						stack_ptr += WORD_SIZE * POPA_WORDS;
					}
				} while (OPCODE_IS_POP_GPR(opcode) ||
					 opcode == OPCODE_POPA);
			}
			if (extra_frame) {
				eip = READ_MEMORY(cpu, stack_ptr);
				if (!SUPPRESS_FRAME(eip)) {
					bool success = add_frame(ls, st, eip);
					if (!success && wrong_cr3)
						return st;
				}

				/* Keep walking looking for more extra frames. */
				if (!iret_block) {
					/* Normal function call. */
					stack_ptr += WORD_SIZE;
				} else if (READ_MEMORY(cpu, stack_ptr + WORD_SIZE) ==
					   SEGSEL_KERNEL_CS) {
					/* Kernel-to-kernel iret. Look past it. */
					stack_ptr += WORD_SIZE * IRET_BLOCK_WORDS;
				} else {
					/* User-to-kernel iret. Stack switch. */
					assert(READ_MEMORY(cpu, stack_ptr + WORD_SIZE)
					       == SEGSEL_USER_CS);
					int esp_addr = stack_ptr + (3 * WORD_SIZE);
					stack_ptr = READ_MEMORY(cpu, esp_addr);
				}
			}
		} while (extra_frame);

		if (ebp == 0) {
			break;
		}

		/* Find pushed return address behind the base pointer. */
		eip = READ_MEMORY(cpu, ebp + WORD_SIZE);
		if (eip == 0) {
			break;
		}
		stack_ptr = ebp + (2 * WORD_SIZE);
		/* Suppress kernel frames if testing user, unless verbose enough. */
		if (!SUPPRESS_FRAME(eip)) {
			bool success = add_frame(ls, st, eip);
			if (!success && wrong_cr3)
				return st;

			/* special-case termination condition -- _start */
			if (eip == GUEST_START) {
				break;
			}
		}

		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, ebp);
		if (rabbit == ebp) stop_ebp = ebp;
		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, ebp);
		if (rabbit == ebp) stop_ebp = ebp;
		ebp = READ_MEMORY(cpu, ebp);
		if ((unsigned int)ebp < GUEST_DATA_START) {
			/* Some kernels allow terminal ebps to trail off into
			 * "junk values". Sometimes these are very small values.
			 * We can avoid emitting simics errors in these cases. */
			ebp = 0;
		}
	}

	return st;
}
