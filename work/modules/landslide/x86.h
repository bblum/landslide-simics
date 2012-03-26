/**
 * @file x86.h
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#ifndef __LS_X86_H
#define __LS_X86_H

#include <simics/api.h>
#include <simics/core/memory.h>

/* reading and writing cpu registers */
#define GET_CPU_ATTR(cpu, name) ((int)SIM_attr_integer(SIM_get_attribute(cpu, #name)))
#define SET_CPU_ATTR(cpu, name, val) do {				\
		attr_value_t noob = SIM_make_attr_integer(val);		\
		set_error_t ret = SIM_set_attribute(cpu, #name, &noob);	\
		assert(ret == Sim_Set_Ok && "SET_CPU_ATTR failed!");	\
	} while (0)

#define WORD_SIZE 4
#define PAGE_SIZE 4096
#define PAGE_ALIGN(x) ((x) & ~(PAGE_SIZE-1))

#define USER_MEM_START 0x01000000

#define KERNEL_SEGSEL_CS 0x10
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
#define OPCODE_INT_ARG(cpu, eip) READ_BYTE(cpu, eip + 1)

#define MEM_TRANSLATE(cpu, addr) /* I am sorry for writing this */	\
	((addr) < USER_MEM_START ? (addr) :				\
	({	int upper = (addr) >> 22;				\
		int lower = ((addr) >> 12) & 1023;			\
		int offset = (addr) & 4095;				\
		int pde = SIM_read_phys_memory(cpu, GET_CPU_ATTR(cpu, cr3) + (4 * upper), WORD_SIZE); \
		int pte = SIM_read_phys_memory(cpu, (pde & ~4095) + (4 * lower), WORD_SIZE); \
		(pte & ~4095) + offset; }))

#define READ_BYTE(cpu, addr) \
	((int)SIM_read_phys_memory(cpu, MEM_TRANSLATE(cpu, addr), 1))
#define READ_MEMORY(cpu, addr) \
	((int)SIM_read_phys_memory(cpu, MEM_TRANSLATE(cpu, addr), WORD_SIZE))

/* reading the stack. can be used to examine function arguments, if used either
 * at the very end or the very beginning of a function, when esp points to the
 * return address. */
#define READ_STACK(cpu, offset) \
	READ_MEMORY(cpu, GET_CPU_ATTR(cpu, esp) + ((offset) * WORD_SIZE))

void cause_timer_interrupt(conf_object_t *cpu);
int cause_timer_interrupt_immediately(conf_object_t *cpu);
int avoid_timer_interrupt_immediately(conf_object_t *cpu);
void cause_keypress(conf_object_t *kbd, char);
bool interrupts_enabled(conf_object_t *cpu);
bool within_function(conf_object_t *cpu, int eip, int func, int func_end);
char *stack_trace(conf_object_t *cpu, int eip, int tid);
char *read_string(conf_object_t *cpu, int eip);

#endif
