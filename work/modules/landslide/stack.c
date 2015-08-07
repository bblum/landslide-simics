/**
 * @file stack.c
 * @brief stack tracing
 * @author Ben Blum
 */

#define MODULE_NAME "STACK"
#define MODULE_COLOUR COLOUR_DARK COLOUR_BLUE

#include "common.h"
#include "html.h"
#include "kernel_specifics.h"
#include "kspec.h"
#include "landslide.h"
#include "stack.h"
#include "symtable.h"
#include "variable_queue.h"
#include "x86.h"

/******************************************************************************
 * printing utilities / glue
 ******************************************************************************/

/* guaranteed not to clobber nobe. */
bool eip_to_frame(unsigned int eip, struct stack_frame *f)
{
	f->eip = eip;
	f->name = NULL;
	f->file = NULL;
	return symtable_lookup(eip, &f->name, &f->file, &f->line);
}

/* Emits a "0xADDR in NAME (FILE:LINE)" line with pretty colours. */
void print_stack_frame(verbosity v, struct stack_frame *f)
{
	printf(v, "0x%.8x in ", f->eip);
	if (f->name == NULL) {
		printf(v, COLOUR_BOLD COLOUR_MAGENTA "<unknown");
		if (USER_MEMORY(f->eip)) {
			printf(v, " in userspace");
		} else if (f->eip > GUEST_DATA_START) {
			printf(v, " in kernel");
		}
		printf(v, ">" COLOUR_DEFAULT);
	} else {
		printf(v, COLOUR_BOLD COLOUR_CYAN "%s "
		       COLOUR_DARK COLOUR_GREY, f->name);
		if (f->file == NULL) {
			printf(v, "<unknown assembly>");
		} else {
			printf(v, "(%s:%d)", f->file, f->line);
		}
		printf(v, COLOUR_DEFAULT);
	}
}

/* Same as print_stack_frame but includes the symbol table lookup. */
void print_eip(verbosity v, unsigned int eip)
{
	struct stack_frame f;
	eip_to_frame(eip, &f);
	print_stack_frame(v, &f);
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

/* Prints a stack trace to a multiline html table. Returns length printed. */
unsigned int html_stack_trace(char *buf, unsigned int maxlen, struct stack_trace *st)
{
#define PRINT(...) do { pos += scnprintf(buf + pos, maxlen - pos, __VA_ARGS__); } while (0)
	unsigned int pos = 0;
	bool first_frame = true;
	struct stack_frame *f;

	Q_FOREACH(f, &st->frames, nobe) {
		if (!first_frame) {
			PRINT("<br />");
		}
		first_frame = false;
		/* see print_stack_frame, above */
		PRINT("0x%.8x in ", f->eip);
		if (f->name == NULL) {
			PRINT(HTML_COLOUR_START(HTML_COLOUR_MAGENTA)
			      "&lt;unknown in %s&gt;" HTML_COLOUR_END,
			      KERNEL_MEMORY(f->eip) ? "kernel" : "user");
		} else {
			PRINT(HTML_COLOUR_START(HTML_COLOUR_CYAN) "<b>%s</b>"
			      HTML_COLOUR_END " "
			      HTML_COLOUR_START(HTML_COLOUR_GREY) "<small>",
			      f->name);
			if (f->file == NULL) {
				PRINT("&lt;unknown assembly&gt;");
			} else {
				PRINT("(%s:%d)", f->file, f->line);
			}
			PRINT("</small>" HTML_COLOUR_END);
		}
		PRINT("\n");
	}

	return pos;
#undef PRINT
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
		assert(f != NULL);
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

static bool splice_pre_vanish_trace(struct ls_state *ls, struct stack_trace *st,
				    unsigned int eip)
{
	struct stack_trace *pvt = ls->sched.cur_agent->pre_vanish_trace;
	bool found_eip = false;

	if (pvt == NULL || KERNEL_MEMORY(eip)) {
		return false;
	}

	struct stack_frame *f;
	Q_FOREACH(f, &pvt->frames, nobe) {
		if (f->eip == eip) {
			found_eip = true;
		}
		if (found_eip) {
			struct stack_frame *newf = MM_XMALLOC(1, struct stack_frame);
			newf->eip  = f->eip;
			newf->name = f->name == NULL ? NULL : MM_XSTRDUP(f->name);
			newf->file = f->file == NULL ? NULL : MM_XSTRDUP(f->file);
			newf->line = f->line;
			Q_INSERT_TAIL(&st->frames, newf, nobe);
		}
	}
	return found_eip;
}

/* Usually stack trace eips are the pushed return address; i.e., the instruction
 * after the "call". But when a 'noreturn' function's last instruction is a call
 * this will result in the subsequent function being shown in the trace, which
 * is of course very confusing. So we special case here... */
static unsigned int check_noreturn_function(conf_object_t *cpu, unsigned int eip)
{
	unsigned int eip_offset;
	if (function_eip_offset(eip, &eip_offset) && eip_offset == 0 &&
	    READ_BYTE(cpu, eip - 5) == OPCODE_CALL) {
		return eip - 5;
	} else {
		return eip;
	}
}

/******************************************************************************
 * actual logic
 ******************************************************************************/

/* returns false if symtable lookup failed */
static bool add_frame(struct stack_trace *st, unsigned int eip)
{
	struct stack_frame *f = MM_XMALLOC(1, struct stack_frame);
	bool lookup_success = eip_to_frame(eip, f);
	Q_INSERT_TAIL(&st->frames, f, nobe);
	return lookup_success;
}

/* Suppress stack frames from userspace, if testing userland, unless the
 * verbosity setting is high enough. */
#define SUPPRESS_FRAME(eip) \
	(MAX_VERBOSITY < DEV && testing_userspace() && KERNEL_MEMORY(eip) )

#ifdef PINTOS_KERNEL
/* The pintos boot sequence puts the initial kstack below the kernel image
 * (approx 0xc000eXXX, compared to 0xc002XXXX). */
#define CHECK_JUNK_EBP_BELOW_TEXT(ebp) false
#else
#define CHECK_JUNK_EBP_BELOW_TEXT(ebp) ((unsigned)(ebp) < GUEST_DATA_START)
#endif

struct stack_trace *stack_trace(struct ls_state *ls)
{
	conf_object_t *cpu = ls->cpu0;
	unsigned int eip = ls->eip;
	unsigned int tid = ls->sched.cur_agent->tid;

	unsigned int stack_ptr = GET_CPU_ATTR(cpu, esp);

	struct stack_trace *st = MM_XMALLOC(1, struct stack_trace);
	st->tid = tid;
	Q_INIT_HEAD(&st->frames);

	/* Add current frame, even if it's in kernel and we're in user. */
	add_frame(st, eip);

	unsigned int stop_ebp = 0;
	unsigned int ebp = GET_CPU_ATTR(cpu, ebp);
	unsigned int rabbit = ebp;
	unsigned int frame_count = 0;

	/* Figure out if the thread is vanishing and we should expect its cr3
	 * to have been freed (so we can't trace in userspace).
	 * (Note this condition will also hit init/shell, but we should not
	 * expect unknown symtable results from them in any case.) */
	bool wrong_cr3 =
		testing_userspace() && GET_CPU_ATTR(cpu, cr3) != ls->user_mem.cr3;

	while (ebp != stop_ebp && (testing_userspace() || KERNEL_MEMORY(ebp))
	       && frame_count++ < 1024) {
		bool extra_frame;

		/* This is the same check as at the end (after ebp advances),
		 * but duplicated here for the corner case where ebp's initial
		 * value is trash. (It's inside the loop so we can still look
		 * for extra frames.) */
		if (CHECK_JUNK_EBP_BELOW_TEXT(ebp)) {
			ebp = 0;
		}

		/* Find "extra frames" of functions that don't set up an ebp
		 * frame or of untimely interrupts, before following ebp. */
		do {
			unsigned int eip_offset;
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
				unsigned int opcode;
				unsigned int opcode_offset = 0;
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
				eip = check_noreturn_function(cpu, eip);
				if (splice_pre_vanish_trace(ls, st, eip)) {
					return st;
				} else if (!SUPPRESS_FRAME(eip)) {
					bool success = add_frame(st, eip);
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
					unsigned int esp_addr =
						stack_ptr + (3 * WORD_SIZE);
					stack_ptr = READ_MEMORY(cpu, esp_addr);
				}
			}
		} while (extra_frame);

		if (ebp == 0) {
			break;
		}

		/* Find pushed return address behind the base pointer. */
		eip = READ_MEMORY(cpu, ebp + WORD_SIZE);
		eip = check_noreturn_function(cpu, eip);
		if (eip == 0) {
			break;
		}
		stack_ptr = ebp + (2 * WORD_SIZE);
		/* Suppress kernel frames if testing user, unless verbose enough. */
		if (splice_pre_vanish_trace(ls, st, eip)) {
			return st;
		} else if (!SUPPRESS_FRAME(eip)) {
			bool success = add_frame(st, eip);
			if (!success && wrong_cr3)
				return st;

			/* special-case termination condition -- _start */
			if (eip == GUEST_START) {
				break;
			}
		}

		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, rabbit);
		if (rabbit == ebp) stop_ebp = ebp;
		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, rabbit);
		if (rabbit == ebp) stop_ebp = ebp;
		ebp = READ_MEMORY(cpu, ebp);
		if (CHECK_JUNK_EBP_BELOW_TEXT(ebp)) {
			/* Some kernels allow terminal ebps to trail off into
			 * "junk values". Sometimes these are very small values.
			 * We can avoid emitting simics errors in these cases. */
			ebp = 0;
		} else if (KERNEL_MEMORY(stack_ptr) && USER_MEMORY(ebp)) {
			/* Base pointer chain crossed over into userspace,
			 * leaving stack pointer behind. This happens in pathos,
			 * whose syscall wrappers craft fake frames to help
			 * simics's tracer continue into userspace. */
#ifdef PATHOS_SYSCALL_IRET_DISTANCE
			/* Find iret frame. */
			stack_ptr += PATHOS_SYSCALL_IRET_DISTANCE;
			assert(USER_MEMORY(READ_MEMORY(cpu, stack_ptr)));
			assert(READ_MEMORY(cpu, stack_ptr + WORD_SIZE)
			       == SEGSEL_USER_CS);
			stack_ptr = READ_MEMORY(cpu, stack_ptr + (3 * WORD_SIZE));
			ASSERT(USER_MEMORY(stack_ptr));
			// TODO: Decide what to do if pathos annotation missing?
			// Most likely, set some "skip_extra_frame" flag.
#endif
		}
	}

	return st;
}

/* As below but doesn't require duplicating the work of making a fresh stack
 * trace if you already have one. .*/
bool within_function_st(struct stack_trace *st, unsigned int func,
			unsigned int func_end)
{
	struct stack_frame *f;
	bool result = false;
	Q_FOREACH(f, &st->frames, nobe) {
		if (f->eip >= func && f->eip <= func_end) {
			result = true;
			break;
		}
	}
	return result;
}

/* Performs a stack trace to see if the current call stack has the given
 * function somewhere on it. */
bool within_function(struct ls_state *ls, unsigned int func, unsigned int func_end)
{
	/* Note while it may seem wasteful to malloc a bunch of times to make
	 * the stack trace, many (not all) of the mallocs are actually needed
	 * because the 'wrong cr3' end condition requires a symtable lookup. */
	struct stack_trace *st = stack_trace(ls);
	bool result = within_function_st(st, func, func_end);
	free_stack_trace(st);
	return result;
}
