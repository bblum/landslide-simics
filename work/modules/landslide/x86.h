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
#define GET_CPU_ATTR(cpu, name) get_cpu_attr(cpu, #name)

static inline int get_cpu_attr(conf_object_t *cpu, const char *name) {
	attr_value_t ebp_attr = SIM_get_attribute(cpu, name);
	if (!SIM_attr_is_integer(ebp_attr)) {
		assert(ebp_attr.kind == Sim_Val_Invalid &&
		       "GET_CPU_ATTR failed!");
		// "Try again." WTF, simics??
		return ((int)SIM_attr_integer(SIM_get_attribute(cpu, name)));
	}
	return ((int)SIM_attr_integer(ebp_attr));
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

#define USER_MEM_START 0x01000000

#define FORK_INT            0x41
#define EXEC_INT            0x42
/* #define EXIT_INT            0x43 */
#define WAIT_INT            0x44
#define YIELD_INT           0x45
#define DESCHEDULE_INT      0x46
#define MAKE_RUNNABLE_INT   0x47
#define GETTID_INT          0x48
#define NEW_PAGES_INT       0x49
#define REMOVE_PAGES_INT    0x4A
#define SLEEP_INT           0x4B
#define GETCHAR_INT         0x4C
#define READLINE_INT        0x4D
#define PRINT_INT           0x4E
#define SET_TERM_COLOR_INT  0x4F
#define SET_CURSOR_POS_INT  0x50
#define GET_CURSOR_POS_INT  0x51
#define THREAD_FORK_INT     0x52
#define GET_TICKS_INT       0x53
#define MISBEHAVE_INT       0x54
#define HALT_INT            0x55
#define LS_INT              0x56
#define TASK_VANISH_INT     0x57 /* previously known as TASK_EXIT_INT */
#define SET_STATUS_INT      0x59
#define VANISH_INT          0x60
#define CAS2I_RUNFLAG_INT   0x61
#define SWEXN_INT           0x74

#define SEGSEL_KERNEL_CS 0x10
#define SEGSEL_KERNEL_DS 0x18
#define SEGSEL_USER_CS 0x23
#define SEGSEL_USER_DS 0x2b
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

#define MEM_TRANSLATE(cpu, addr) /* I am sorry for writing this */	\
	(((unsigned int)(addr)) < ((unsigned int)USER_MEM_START) ? (addr) :	\
	({	unsigned int upper = ((unsigned int)(addr)) >> 22;		\
		unsigned int lower = (((unsigned int)(addr)) >> 12) & 1023;	\
		unsigned int offset = ((unsigned int)(addr)) & 4095;		\
		unsigned int pde = SIM_read_phys_memory(cpu, GET_CPU_ATTR(cpu, cr3) + (4 * upper), WORD_SIZE); \
		unsigned int pte = SIM_read_phys_memory(cpu, (pde & ~4095) + (4 * lower), WORD_SIZE); \
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
char *read_string(conf_object_t *cpu, int eip);
bool instruction_is_atomic_swap(conf_object_t *cpu, int eip);

#endif
