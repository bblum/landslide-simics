/* The 15-410 kernel.
 * syscall.h
 *
 * Prototypes for the user-land C library interface
 * to system calls.
 *
 */

#ifndef _SYSCALL_H
#define _SYSCALL_H

#define NORETURN __attribute__((__noreturn__))

#define PAGE_SIZE 0x0001000 /* 4096 */

/* Life cycle */
int fork(void);
int exec(char *execname, char *argvec[]);
void set_status(int status);
void vanish(void) NORETURN;
int wait(int *status_ptr);
void task_vanish(int status) NORETURN;

/* Thread management */
int gettid(void);
int yield(int pid);
int deschedule(int *flag);
int make_runnable(int pid);
unsigned int get_ticks(void);
int sleep(int ticks);

/* Memory management */
int new_pages(void * addr, int len);
int remove_pages(void * addr);

/* Console I/O */
char getchar(void);
int readline(int size, char *buf);
int print(int size, char *buf);
int set_term_color(int color);
int set_cursor_pos(int row, int col);
int get_cursor_pos(int *row, int *col);

/* Color values for set_term_color() */
#define FGND_BLACK 0x0
#define FGND_BLUE  0x1
#define FGND_GREEN 0x2
#define FGND_CYAN  0x3
#define FGND_RED   0x4
#define FGND_MAG   0x5
#define FGND_BRWN  0x6
#define FGND_LGRAY 0x7 /* Light gray. */
#define FGND_DGRAY 0x8 /* Dark gray. */
#define FGND_BBLUE 0x9 /* Bright blue. */
#define FGND_BGRN  0xA /* Bright green. */
#define FGND_BCYAN 0xB /* Bright cyan. */
#define FGND_PINK  0xC
#define FGND_BMAG  0xD /* Bright magenta. */
#define FGND_YLLW  0xE
#define FGND_WHITE 0xF

#define BGND_BLACK 0x00
#define BGND_BLUE  0x10
#define BGND_GREEN 0x20
#define BGND_CYAN  0x30
#define BGND_RED   0x40
#define BGND_MAG   0x50
#define BGND_BRWN  0x60
#define BGND_LGRAY 0x70 /* Light gray. */

/* Miscellaneous */
void halt();
int readfile(char *filename, char *buf, int count, int offset);

/* "Special" */
void misbehave(int mode);

/* Project 4 F2010 */
#include <ureg.h> /* may be directly included by kernel guts */
typedef void (*swexn_handler_t)(void *arg, ureg_t *ureg);
int swexn(void *esp3, swexn_handler_t eip, void *arg, ureg_t *newureg);

/* Previous API */
/*
void exit(int status) NORETURN;
void task_exit(int status) NORETURN;
int cas2i_runflag(int tid, int *oldp, int ev1, int nv1, int ev2, int nv2);
int ls(int size, char *buf);
*/

#endif /* _SYSCALL_H */
