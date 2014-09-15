/* The 15-410 reference kernel.
 * syscall_int.h
 *
 * Prototypes for assembly language wrappers for the
 * system call interface.
 *
 * Adam Chlipala (adamc)
 * Jorge Vittes (jvittes)
 * Zachary Anderson (zra)
 */

#ifndef _SYSCALL_INT_H
#define _SYSCALL_INT_H

#define SYSCALL_INT         0x40
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
/* #define LS_INT              0x56 */
#define TASK_VANISH_INT     0x57 /* previously known as TASK_EXIT_INT */
#define SET_STATUS_INT      0x59
#define VANISH_INT          0x60
/* #define CAS2I_RUNFLAG_INT   0x61 */
#define READFILE_INT        0x62

#define SWEXN_INT           0x74

/* The syscalls in here, INCLUSIVE, are promised not to be
 * probed by any grading scripts; as such you are welcome
 * to extend the spec by making use of these syscall numbers
 */
#define SYSCALL_RESERVED_START    0x80
#define SYSCALL_RESERVED_0        0x80
#define SYSCALL_RESERVED_1        0x81
#define SYSCALL_RESERVED_2        0x82
#define SYSCALL_RESERVED_3        0x83
#define SYSCALL_RESERVED_4        0x84
#define SYSCALL_RESERVED_5        0x85
#define SYSCALL_RESERVED_6        0x86
#define SYSCALL_RESERVED_7        0x87
#define SYSCALL_RESERVED_8        0x88
#define SYSCALL_RESERVED_9        0x89
#define SYSCALL_RESERVED_10       0x8A
#define SYSCALL_RESERVED_11       0x8B
#define SYSCALL_RESERVED_12       0x8C
#define SYSCALL_RESERVED_13       0x8D
#define SYSCALL_RESERVED_14       0x8E
#define SYSCALL_RESERVED_15       0x8F
#define SYSCALL_RESERVED_END      0x8F

#endif /* _SYSCALL_INT_H */
