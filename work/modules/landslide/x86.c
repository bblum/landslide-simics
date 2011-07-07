/**
 * @file x86.c
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#include <assert.h>

#include <simics/api.h>

#include "landslide.h"
#include "x86.h"

/* two possible methods for causing a timer interrupt - the lolol version crafts
 * an iret stack frame by hand and changes the cpu's registers manually; the
 * other way just manipulates the cpu's interrupt pending flags to make it do
 * the interrupt itself. */
void cause_timer_interrupt(struct ls_state *ls)
{
// #define CAUSE_TIMER_LOLOL
#ifdef CAUSE_TIMER_LOLOL
# define TIMER_HANDLER_WRAPPER 0x001035bc // TODO: reduce discosity
# define KERNEL_SEGSEL_CS 0x10

	int esp = GET_CPU_ATTR(ls->cpu0, esp);
	int eip = GET_CPU_ATTR(ls->cpu0, eip);
	int eflags = GET_CPU_ATTR(ls->cpu0, eflags);

	/* 12 is the size of an IRET frame only when already in kernel mode. */
	SET_CPU_ATTR(ls->cpu0, esp, esp - 12);
	esp = esp - 12; /* "oh, I can do common subexpression elimination!" */
	SIM_write_phys_memory(ls->cpu0, esp + 8, eflags, 4);
	SIM_write_phys_memory(ls->cpu0, esp + 4, KERNEL_SEGSEL_CS, 4);
	SIM_write_phys_memory(ls->cpu0, esp + 0, eip, 4);
	SET_CPU_ATTR(ls->cpu0, eip, TIMER_HANDLER_WRAPPER);

#else
# define TIMER_INTERRUPT_NUMBER 0x20

	if (GET_CPU_ATTR(ls->cpu0, pending_vector_valid)) {
		SET_CPU_ATTR(ls->cpu0, pending_vector,
			     GET_CPU_ATTR(ls->cpu0, pending_vector)
			     | TIMER_INTERRUPT_NUMBER);
	} else {
		SET_CPU_ATTR(ls->cpu0, pending_vector, TIMER_INTERRUPT_NUMBER);
		SET_CPU_ATTR(ls->cpu0, pending_vector_valid, 1);
	}

	SET_CPU_ATTR(ls->cpu0, pending_interrupt, 1);

#endif
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

void cause_keypress(struct ls_state *ls, char key)
{
	int keycode = i8042_key(key);

	attr_value_t i = SIM_make_attr_integer(keycode);
	attr_value_t v = SIM_make_attr_integer(0); /* see i8042 docs */

	set_error_t ret = SIM_set_attribute_idx(ls->kbd0, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress press failed!");

	v = SIM_make_attr_integer(1);
	ret = SIM_set_attribute_idx(ls->kbd0, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress release failed!");
}

#define EFL_IF          0x00000200 /* from 410kern/inc/x86/eflags.h */

bool interrupts_enabled(struct ls_state *ls)
{
	int eflags = GET_CPU_ATTR(ls->cpu0, eflags);
	return (eflags & EFL_IF) != 0;
}
