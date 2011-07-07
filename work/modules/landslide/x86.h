/**
 * @file x86.h
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#ifndef __LS_X86_H
#define __LS_X86_H

#include <simics/api.h>

/* reading and writing cpu registers */
#define GET_CPU_ATTR(cpu, name) SIM_attr_integer(SIM_get_attribute(cpu, #name))
#define SET_CPU_ATTR(cpu, name, val) do {				\
		attr_value_t noob = SIM_make_attr_integer(val);		\
		set_error_t ret = SIM_set_attribute(cpu, #name, &noob);	\
		assert(ret == Sim_Set_Ok && "SET_CPU_ATTR failed!");	\
	} while (0)

#define WORD_SIZE 4

/* reading the stack. can be used to examine function arguments, if used either
 * at the very end or the very beginning of a function, when esp points to the
 * return address. */
#define READ_STACK(cpu, offset)  \
	SIM_read_phys_memory( \
		cpu, GET_CPU_ATTR(cpu, esp) + ((offset) * WORD_SIZE), WORD_SIZE)

void cause_timer_interrupt(struct ls_state *);
void cause_keypress(struct ls_state *, char);
bool interrupts_enabled(struct ls_state *);

#endif
