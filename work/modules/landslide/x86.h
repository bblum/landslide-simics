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

#define USER_MEM_START 0x01000000

#define READ_MEMORY(cpu, addr) SIM_read_phys_memory(cpu, addr, WORD_SIZE)

/* reading the stack. can be used to examine function arguments, if used either
 * at the very end or the very beginning of a function, when esp points to the
 * return address. */
#define READ_STACK(cpu, offset) \
	READ_MEMORY(cpu, GET_CPU_ATTR(cpu, esp) + ((offset) * WORD_SIZE))

void cause_timer_interrupt(conf_object_t *cpu);
int cause_timer_interrupt_immediately(conf_object_t *cpu);
void cause_keypress(conf_object_t *kbd, char);
bool interrupts_enabled(conf_object_t *cpu);
bool within_function(conf_object_t *cpu, int eip, int func, int func_end);

#endif
