/**
 * @file kspec.h
 * @brief Kernel specification
 * @author Ben Blum
 */

#ifndef __LS_KSPEC_H
#define __LS_KSPEC_H

#include "compiler.h"
#include "student_specifics.h" /* to know whether pebbles or pintos */

#ifdef PINTOS_KERNEL

/******** PINTOS ********/

#define SEGSEL_KERNEL_CS 0x08
#define SEGSEL_KERNEL_DS 0x10
#define SEGSEL_USER_CS 0x1B
#define SEGSEL_USER_DS 0x23

#define KERN_MEM_START 0xc0000000
#define KERNEL_MEMORY(addr) ({ ASSERT_UNSIGNED(addr); (addr) >= KERN_MEM_START; })
#define USER_MEMORY(addr)   ({ ASSERT_UNSIGNED(addr); (addr) <  KERN_MEM_START; })

// TODO - different syscall interface. Numbers defined in lib/syscall-nr.h.
#define FORK_INT            0x41
#define EXEC_INT            0x42
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
#define TASK_VANISH_INT     0x57
#define SET_STATUS_INT      0x59
#define VANISH_INT          0x60
#define CAS2I_RUNFLAG_INT   0x61
#define SWEXN_INT           0x74

#else

/******** PEBBLES ********/

#define SEGSEL_KERNEL_CS 0x10
#define SEGSEL_KERNEL_DS 0x18
#define SEGSEL_USER_CS 0x23
#define SEGSEL_USER_DS 0x2b

#define USER_MEM_START 0x01000000
#define KERNEL_MEMORY(addr) ({ ASSERT_UNSIGNED(addr); (addr) <  USER_MEM_START; })
#define USER_MEMORY(addr)   ({ ASSERT_UNSIGNED(addr); (addr) >= USER_MEM_START; })

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

#endif

#endif
