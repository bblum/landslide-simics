/**
 * @file x86.c
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#include <assert.h>

#include <simics/api.h>

#define MODULE_NAME "X86"
#define MODULE_COLOUR COLOUR_DARK COLOUR_GREEN

#include "common.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "x86.h"

/* two possible methods for causing a timer interrupt - the lolol version crafts
 * an iret stack frame by hand and changes the cpu's registers manually; the
 * other way just manipulates the cpu's interrupt pending flags to make it do
 * the interrupt itself. */
int cause_timer_interrupt_immediately(conf_object_t *cpu)
{
	int esp = GET_CPU_ATTR(cpu, esp);
	int eip = GET_CPU_ATTR(cpu, eip);
	int eflags = GET_CPU_ATTR(cpu, eflags);
	int handler = kern_get_timer_wrap_begin();

	lsprintf(DEV, "tock! (0x%x)\n", eip);

	/* 12 is the size of an IRET frame only when already in kernel mode. */
	SET_CPU_ATTR(cpu, esp, esp - 12);
	esp = esp - 12; /* "oh, I can do common subexpression elimination!" */
	SIM_write_phys_memory(cpu, esp + 8, eflags, 4);
	SIM_write_phys_memory(cpu, esp + 4, KERNEL_SEGSEL_CS, 4);
	SIM_write_phys_memory(cpu, esp + 0, eip, 4);
	SET_CPU_ATTR(cpu, eip, handler);

	return handler;
}

/* i.e., with stallin' */
static void cause_timer_interrupt_soviet_style(conf_object_t *cpu, lang_void *x)
{
	SIM_stall_cycle(cpu, 0);
}

void cause_timer_interrupt(conf_object_t *cpu)
{
	lsprintf(DEV, "tick! (0x%x)\n", (int)GET_CPU_ATTR(cpu, eip));

	if (GET_CPU_ATTR(cpu, pending_vector_valid)) {
		SET_CPU_ATTR(cpu, pending_vector,
			     GET_CPU_ATTR(cpu, pending_vector)
			     | TIMER_INTERRUPT_NUMBER);
	} else {
		SET_CPU_ATTR(cpu, pending_vector, TIMER_INTERRUPT_NUMBER);
		SET_CPU_ATTR(cpu, pending_vector_valid, 1);
	}

	SET_CPU_ATTR(cpu, pending_interrupt, 1);
	/* Causes simics to flush whatever pipeline, implicit or not, would
	 * otherwise let more instructions get executed before the interrupt be
	 * taken. */
	SIM_run_unrestricted(cpu, cause_timer_interrupt_soviet_style, NULL);
}

/* Will use 8 bytes of stack when it runs. */
#define CUSTOM_ASSEMBLY_CODES_SIZE 12
#define CUSTOM_ASSEMBLY_CODES_STACK 8
static const char custom_assembly_codes[] = {
	0x50, /* push %eax */
	0x52, /* push %edx */
	0x66, 0xba, 0x20, 0x00, /* mov $0x20, %dx # INT_ACK_CURRENT */
	0xb0, 0x20, /* mov $0x20, %al # INT_CTL_PORT */
	0xee, /* out %al, (%dx) */
	0x5a, /* pop %edx */
	0x58, /* pop %eax */
	0xcf, /* iret */
};

int avoid_timer_interrupt_immediately(conf_object_t *cpu)
{
	int buf = GET_CPU_ATTR(cpu, esp) -
		(CUSTOM_ASSEMBLY_CODES_SIZE + CUSTOM_ASSEMBLY_CODES_STACK);

	lsprintf(INFO, "Cuckoo!\n");

	STATIC_ASSERT(ARRAY_SIZE(custom_assembly_codes) ==
		      CUSTOM_ASSEMBLY_CODES_SIZE);
	for (int i = 0; i < CUSTOM_ASSEMBLY_CODES_SIZE; i++) {
		SIM_write_phys_memory(cpu, buf+i, custom_assembly_codes[i], 1);
	}

	SET_CPU_ATTR(cpu, eip, buf);
	return buf;
}

/* keycodes for the keyboard buffer */
static int i8042_key(char c)
{
	static const int i8042_keys[] = {
		['0'] = 18, ['1'] = 19, ['2'] = 20, ['3'] = 21, ['4'] = 22,
		['5'] = 23, ['6'] = 24, ['7'] = 25, ['8'] = 26, ['9'] = 27,
		['a'] = 28, ['b'] = 29, ['c'] = 30, ['d'] = 31, ['e'] = 32,
		['f'] = 33, ['g'] = 34, ['h'] = 35, ['i'] = 36, ['j'] = 37,
		['k'] = 38, ['l'] = 39, ['m'] = 40, ['n'] = 41, ['o'] = 42,
		['p'] = 43, ['q'] = 44, ['r'] = 45, ['s'] = 46, ['t'] = 47,
		['u'] = 48, ['v'] = 49, ['w'] = 50, ['x'] = 51, ['y'] = 52,
		['z'] = 53, ['\''] = 54, [','] = 55, ['.'] = 56, [';'] = 57,
		['='] = 58, ['/'] = 59, ['\\'] = 60, [' '] = 61, ['['] = 62,
		[']'] = 63, ['-'] = 64, ['`'] = 65,             ['\n'] = 67,
	};
	assert(i8042_keys[(int)c] != 0 && "Attempt to type an unsupported key");
	return i8042_keys[(int)c];
}

static bool i8042_shift_key(char *c)
{
	static const char i8042_shift_keys[] = {
		['~'] = '`', ['!'] = '1', ['@'] = '2', ['#'] = '3', ['$'] = '4',
		['%'] = '5', ['^'] = '6', ['&'] = '7', ['*'] = '8', ['('] = '9',
		[')'] = '0', ['_'] = '-', ['+'] = '=', ['Q'] = 'q', ['W'] = 'w',
		['E'] = 'e', ['R'] = 'r', ['T'] = 't', ['Y'] = 'y', ['U'] = 'u',
		['I'] = 'i', ['O'] = 'o', ['P'] = 'p', ['{'] = '[', ['}'] = ']',
		['A'] = 'a', ['S'] = 's', ['D'] = 'd', ['D'] = 'd', ['F'] = 'f',
		['G'] = 'g', ['H'] = 'h', ['J'] = 'j', ['K'] = 'k', ['L'] = 'l',
		[':'] = ';', ['"'] = '\'', ['Z'] = 'z', ['X'] = 'x',
		['C'] = 'c', ['V'] = 'v', ['B'] = 'b', ['N'] = 'n', ['M'] = 'm',
		['<'] = ',', ['>'] = '.', ['?'] = '/',
	};

	if (i8042_shift_keys[(int)*c] != 0) {
		*c = i8042_shift_keys[(int)*c];
		return true;
	}
	return false;
}

void cause_keypress(conf_object_t *kbd, char key)
{
	bool do_shift = i8042_shift_key(&key);

	int keycode = i8042_key(key);

	attr_value_t i = SIM_make_attr_integer(keycode);
	attr_value_t v = SIM_make_attr_integer(0); /* see i8042 docs */
	/* keycode value for shift found by trial and error :< */
	attr_value_t shift = SIM_make_attr_integer(72);

	set_error_t ret;

	/* press key */
	if (do_shift) {
		ret = SIM_set_attribute_idx(kbd, "key_event", &shift, &v);
		assert(ret == Sim_Set_Ok && "shift press failed!");
	}

	ret = SIM_set_attribute_idx(kbd, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress press failed!");

	/* release key */
	v = SIM_make_attr_integer(1);
	ret = SIM_set_attribute_idx(kbd, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress release failed!");

	if (do_shift) {
		ret = SIM_set_attribute_idx(kbd, "key_event", &shift, &v);
		assert(ret == Sim_Set_Ok && "cause_keypress release failed!");
	}
}

bool interrupts_enabled(conf_object_t *cpu)
{
	int eflags = GET_CPU_ATTR(cpu, eflags);
	return (eflags & EFL_IF) != 0;
}

/* Performs a stack trace to see if the current call stack has the given
 * function somewhere on it. */
// FIXME: make this as intelligent as stack_trace.
bool within_function(conf_object_t *cpu, int eip, int func, int func_end)
{
	if (eip >= func && eip < func_end)
		return true;

	eip = READ_STACK(cpu, 0);

	if (eip >= func && eip < func_end)
		return true;

	int stop_ebp = 0;
	int ebp = GET_CPU_ATTR(cpu, ebp);
	int rabbit = ebp;
	int frame_count = 0;

	while (ebp != stop_ebp && (unsigned)ebp < USER_MEM_START && frame_count++ < 1024) {
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
#define ADD_FRAME(buf, pos, maxlen, eip) do {				\
	if (eip == kern_get_timer_wrap_begin()) {			\
		pos += snprintf(buf + pos, maxlen - pos, "<timer_wrapper>"); \
	} else {							\
		pos += symtable_lookup(buf + pos, maxlen - pos, eip);	\
	}								\
	} while (0)
#define ENTRY_POINT "_start "
/* Caller has to free the return value. */
char *stack_trace(conf_object_t *cpu, int eip, int tid)
{
	char *buf = MM_XMALLOC(MAX_TRACE_LEN, char);
	int pos = 0, old_pos;
	int stack_offset = 0; /* Counts by 1 - READ_STACK already multiplies */

	ADD_STR(buf, pos, MAX_TRACE_LEN, "TID%d at 0x%.8x in ", tid, eip);
	ADD_FRAME(buf, pos, MAX_TRACE_LEN, eip);

	int stop_ebp = 0;
	int ebp = GET_CPU_ATTR(cpu, ebp);
	int rabbit = ebp;
	int frame_count = 0;

	while (ebp != 0 && (unsigned)ebp < USER_MEM_START && frame_count++ < 1024) {
		bool extra_frame;

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
					stack_offset++;
					extra_frame = true;
				}
			}
			if (!extra_frame) {
				int opcode = READ_BYTE(cpu, eip);
				if (opcode == OPCODE_RET) {
					extra_frame = true;
				} else if (opcode == OPCODE_IRET) {
					iret_block = true;
					extra_frame = true;
				}
			}
			if (extra_frame) {
				eip = READ_STACK(cpu, stack_offset);
				ADD_STR(buf, pos, MAX_TRACE_LEN, "%s0x%.8x in ",
					STACK_TRACE_SEPARATOR, eip);
				ADD_FRAME(buf, pos, MAX_TRACE_LEN, eip);
				if (iret_block)
					stack_offset += IRET_BLOCK_WORDS;
				else
					stack_offset++;;
			}
		} while (extra_frame);

		/* pushed return address behind the base pointer */
		eip = READ_MEMORY(cpu, ebp + WORD_SIZE);
		stack_offset = ebp + 2;
		ADD_STR(buf, pos, MAX_TRACE_LEN, "%s0x%.8x in ",
			STACK_TRACE_SEPARATOR, eip);
		old_pos = pos;
		ADD_FRAME(buf, pos, MAX_TRACE_LEN, eip);
		/* special-case termination condition */
		if (pos - old_pos >= strlen(ENTRY_POINT) &&
		    strncmp(buf + old_pos, ENTRY_POINT,
		            strlen(ENTRY_POINT)) == 0) {
			break;
		}

		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, ebp);
		if (rabbit == ebp) stop_ebp = ebp;
		if (rabbit != stop_ebp) rabbit = READ_MEMORY(cpu, ebp);
		if (rabbit == ebp) stop_ebp = ebp;
		ebp = READ_MEMORY(cpu, ebp);
	}

	char *buf2 = MM_XSTRDUP(buf); /* truncate to save space */
	MM_FREE(buf);

	return buf2;
}

char *read_string(conf_object_t *cpu, int addr)
{
	int length = 0;

	while (READ_BYTE(cpu, addr + length) != 0) {
		length++;
	}

	char *buf = MM_XMALLOC(length + 1, char);

	for (int i = 0; i <= length; i++) {
		buf[i] = READ_BYTE(cpu, addr + i);
	}

	return buf;
}
