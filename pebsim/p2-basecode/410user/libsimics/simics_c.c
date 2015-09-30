#include <simics.h>
#include <stdarg.h>
#include <stdio.h>
#include <syscall.h>    /* for readline() */

int sim_in_simics(void) {
    return sim_call(SIM_IN_SIMICS);
}

int sim_memsize(void) {
    return sim_call(SIM_MEMSIZE);
}

void sim_puts(const char *arg) {
    sim_call(SIM_PUTS, arg);
}

void sim_breakpoint(void) {
    sim_call(SIM_BREAKPOINT);
}

void sim_halt(void) {
    sim_call(SIM_HALT);
}

void sim_ck1(void) {
    sim_call(SIM_CK1);
}

void sim_fr_prog(int a) {
    sim_call(SIM_FR_PROG, a);
}

void sim_fr_here(void) {
    sim_call(SIM_FR_HERE);
}

void sim_printf(const char *fmt, ...) {
    char str[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(str, sizeof(str) - 1, fmt, ap);
    va_end(ap);

    sim_puts(str);
}

void sim_update_scoreboard(char *testname, int success) {
    sim_call(SIM_TEST_REPORT, testname, success);
}

int magic_readline(int size, char *buf) {
    sim_call(SIM_FR_INKEYS);
    return readline(size, buf);
}
