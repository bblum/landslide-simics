/** @file lib/simics.h
 *  @brief Simics interface (kernel side)
 *  @author matthewj S2008
 */

#ifndef LIB_SIMICS_H
#define LIB_SIMICS_H

#ifdef ASSEMBLER
#define lprintf sim_printf
#endif

#define SIM_IN_SIMICS       0x04100000
#define SIM_MEMSIZE         0x04100001
#define SIM_PUTS            0x04100002
#define SIM_BREAKPOINT      0x04100003
#define SIM_HALT            0x04100004

#define SIM_CK1             0x04108000
#define SIM_FR_INKEYS       0x04108004
#define SIM_FR_PROG         0x04108006
#define SIM_FR_HERE         0x04108007

#define SIM_TEST_REPORT     0x0410800B

#ifdef ASSEMBLER

/* Now *this* is a fun macro. It expands into a simics call to 'num' with
 * arguments 'arg0' and 'arg1'. For use in assembly functions, to call simics
 * without touching the stack. */

#define INLINE_SIMCALL(num, arg0, arg1) \
    mov $num, %ebx; \
    mov arg0, %ecx; \
    mov arg1, %edx; \
    xchg %ebx, %ebx;

#endif

#ifndef ASSEMBLER

/** @brief Calls simics. Arguments are ebx, ecx, edx. */
extern int sim_call(int ebx, ...);

/** @brief Returns whether we are in simics */
extern int sim_in_simics(void);

/** @brief Returns machine memory size, from simics */
extern int sim_memsize(void);

/** @brief Prints a string to the simics console */
extern void sim_puts(const char *arg);

/** @brief Breakpoint */
extern void sim_breakpoint(void);

/** @brief Halt the simulation */
extern void sim_halt(void);

/** @brief Run the checkpoint 1 checker. */
extern void sim_ck1(void);

/** @brief Grading harness stuff */
extern void sim_fr_prog(int a);
extern void sim_fr_here(void);

/** @brief Convenience wrapper around sim_puts(). */
extern void sim_printf(const char *fmt, ...) __attribute__((__format__ (__printf__, 1, 2)));

/** @brief readline(), but wrapped in "magic" */
int magic_readline(int size, char *buf);

void sim_update_scoreboard(char *testname, int success);

/* "Compatibility mode" for old code */
#define MAGIC_BREAK sim_breakpoint()
#define lprintf(...) sim_printf(__VA_ARGS__)

#endif /* !ASSEMBLER */

#endif /* !LIB_SIMICS_H */
