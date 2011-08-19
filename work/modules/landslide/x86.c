/**
 * @file x86.c
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#include <assert.h>

#include <simics/api.h>

#include "x86.h"
#include "kernel_specifics.h"

/* two possible methods for causing a timer interrupt - the lolol version crafts
 * an iret stack frame by hand and changes the cpu's registers manually; the
 * other way just manipulates the cpu's interrupt pending flags to make it do
 * the interrupt itself. */
#define KERNEL_SEGSEL_CS 0x10
void cause_timer_interrupt_immediately(conf_object_t *cpu)
{
	int esp = GET_CPU_ATTR(cpu, esp);
	int eip = GET_CPU_ATTR(cpu, eip);
	int eflags = GET_CPU_ATTR(cpu, eflags);

	/* 12 is the size of an IRET frame only when already in kernel mode. */
	SET_CPU_ATTR(cpu, esp, esp - 12);
	esp = esp - 12; /* "oh, I can do common subexpression elimination!" */
	SIM_write_phys_memory(cpu, esp + 8, eflags, 4);
	SIM_write_phys_memory(cpu, esp + 4, KERNEL_SEGSEL_CS, 4);
	SIM_write_phys_memory(cpu, esp + 0, eip, 4);
	SET_CPU_ATTR(cpu, eip, kern_get_timer_wrap_begin());
}

/* i.e., with stallin' */
static void cause_timer_interrupt_soviet_style(conf_object_t *cpu, lang_void *x)
{
	SIM_stall_cycle(cpu, 0);
}

#define TIMER_INTERRUPT_NUMBER 0x20
void cause_timer_interrupt(conf_object_t *cpu)
{
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

#define EFL_IF          0x00000200 /* from 410kern/inc/x86/eflags.h */

bool interrupts_enabled(conf_object_t *cpu)
{
	int eflags = GET_CPU_ATTR(cpu, eflags);
	return (eflags & EFL_IF) != 0;
}
