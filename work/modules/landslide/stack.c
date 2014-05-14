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
#include "stack.h"
#include "x86.h"

/* Performs a stack trace to see if the current call stack has the given
 * function somewhere on it. */
// FIXME: make this as intelligent as stack_trace.
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
#define ENTRY_POINT "_start "

/* Suppress stack frames from userspace, if testing userland, unless the
 * verbosity setting is high enough. */
#define SUPPRESS_FRAME(eip) \
	(MAX_VERBOSITY < DEV && testing_userspace() && \
	 (unsigned int)(eip) < USER_MEM_START)

/* Caller has to free the return value. */
char *stack_trace(struct ls_state *ls)
{
	conf_object_t *cpu = ls->cpu0;
	int eip = ls->eip;
	int tid = ls->sched.cur_agent->tid;

	char *buf = MM_XMALLOC(MAX_TRACE_LEN, char);
	char *buf2;
	int pos = 0, old_pos;
	int stack_ptr = GET_CPU_ATTR(cpu, esp);
	bool frame_unknown;

	/* Add current frame, even if it's in kernel and we're in user. */
	ADD_STR(buf, pos, MAX_TRACE_LEN, "TID%d at 0x%.8x in ", tid, eip);
	ADD_FRAME(buf, pos, MAX_TRACE_LEN, eip, &frame_unknown);

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
					ADD_STR(buf, pos, MAX_TRACE_LEN, "%s0x%.8x in ",
						STACK_TRACE_SEPARATOR, eip);
					ADD_FRAME(buf, pos, MAX_TRACE_LEN, eip,
						  &frame_unknown);
					if (frame_unknown && wrong_cr3)
						goto done;
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
			ADD_STR(buf, pos, MAX_TRACE_LEN, "%s0x%.8x in ",
				STACK_TRACE_SEPARATOR, eip);
			old_pos = pos;
			ADD_FRAME(buf, pos, MAX_TRACE_LEN, eip, &frame_unknown);
			if (frame_unknown && wrong_cr3)
				goto done;
			/* special-case termination condition */
			if (pos - old_pos >= strlen(ENTRY_POINT) &&
			    strncmp(buf + old_pos, ENTRY_POINT,
				    strlen(ENTRY_POINT)) == 0) {
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

done:
	buf2 = MM_XSTRDUP(buf); /* truncate to save space */
	MM_FREE(buf);

	return buf2;
}
