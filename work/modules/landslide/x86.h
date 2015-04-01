/**
 * @file x86.h
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#ifndef __LS_X86_H
#define __LS_X86_H

#include <simics/api.h>
#include <simics/core/memory.h>

#include "compiler.h"

/* reading and writing cpu registers */
#define GET_CPU_ATTR(cpu, name) get_cpu_attr(cpu, #name)

static inline unsigned int get_cpu_attr(conf_object_t *cpu, const char *name) {
	attr_value_t register_attr = SIM_get_attribute(cpu, name);
	if (!SIM_attr_is_integer(register_attr)) {
		assert(register_attr.kind == Sim_Val_Invalid && "GET_CPU_ATTR failed!");
		// "Try again." WTF, simics??
		register_attr = SIM_get_attribute(cpu, name);
		assert(SIM_attr_is_integer(register_attr));
		return SIM_attr_integer(register_attr);
	}
	return SIM_attr_integer(register_attr);
}
#define SET_CPU_ATTR(cpu, name, val) do {				\
		attr_value_t noob = SIM_make_attr_integer(val);		\
		set_error_t ret = SIM_set_attribute(cpu, #name, &noob);	\
		assert(ret == Sim_Set_Ok && "SET_CPU_ATTR failed!");	\
		SIM_free_attribute(noob);				\
	} while (0)

#define WORD_SIZE 4
#define PAGE_SIZE 4096
#define PAGE_ALIGN(x) ((x) & ~(PAGE_SIZE-1))

#define CR0_PG (1 << 31)
#define TIMER_INTERRUPT_NUMBER 0x20
#define INT_CTL_PORT 0x20 /* MASTER_ICW == ADDR_PIC_BASE + OFF_ICW */
#define INT_ACK_CURRENT 0x20 /* NON_SPEC_EOI */
#define EFL_IF          0x00000200 /* from 410kern/inc/x86/eflags.h */
#define OPCODE_PUSH_EBP 0x55
#define OPCODE_RET  0xc3
#define OPCODE_IRET 0xcf
#define IRET_BLOCK_WORDS 3
#define OPCODE_HLT 0xf4
#define OPCODE_INT 0xcd
#define OPCODE_IS_POP_GPR(o) ((o) >= 0x58 && (o) < 0x60)
#define OPCODE_POPA 0x61
#define POPA_WORDS 8
#define OPCODE_INT_ARG(cpu, eip) READ_BYTE(cpu, eip + 1)

void cause_timer_interrupt(conf_object_t *cpu);
unsigned int cause_timer_interrupt_immediately(conf_object_t *cpu);
unsigned int avoid_timer_interrupt_immediately(conf_object_t *cpu);
void cause_keypress(conf_object_t *kbd, char);
bool interrupts_enabled(conf_object_t *cpu);
unsigned int read_memory(conf_object_t *cpu, unsigned int addr, unsigned int width);
char *read_string(conf_object_t *cpu, unsigned int eip);
bool instruction_is_atomic_swap(conf_object_t *cpu, unsigned int eip);
unsigned int delay_instruction(conf_object_t *cpu);

#define READ_BYTE(cpu, addr) \
	({ ASSERT_UNSIGNED(addr); read_memory(cpu, addr, 1); })
#define READ_MEMORY(cpu, addr) \
	({ ASSERT_UNSIGNED(addr); read_memory(cpu, addr, WORD_SIZE); })

/* reading the stack. can be used to examine function arguments, if used either
 * at the very end or the very beginning of a function, when esp points to the
 * return address. */
#define READ_STACK(cpu, offset) \
	READ_MEMORY(cpu, GET_CPU_ATTR(cpu, esp) + ((offset) * WORD_SIZE))

#endif
