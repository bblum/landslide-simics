/**
 * @file kernel_specifics_pobbles_race.h
 * @brief #defines for the pobbles_race guest kernel (automatically generated)
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_KERNEL_SPECIFICS_POBBLES_RACE_H
#define __LS_KERNEL_SPECIFICS_POBBLES_RACE_H

#define GUEST_CURRENT_TCB 0x001564a0
#define GUEST_TCB_TID_OFFSET 0
#define TID_FROM_TCB(cpu, tcb) \
	SIM_read_phys_memory(cpu, tcb + GUEST_TCB_TID_OFFSET, WORD_SIZE)

#define GUEST_RQ_ADDR 0x001564a4

#define GUEST_Q_ADD                0x001056a1
#define GUEST_Q_ADD_Q_ARGNUM       1
#define GUEST_Q_ADD_TCB_ARGNUM     2
#define GUEST_Q_REMOVE             0x00105763
#define GUEST_Q_REMOVE_Q_ARGNUM    1
#define GUEST_Q_REMOVE_TCB_ARGNUM  2
#define GUEST_Q_POP_RETURN         0x105762
#define GUEST_Q_POP_Q_ARGNUM       1

#define GUEST_TIMER_WRAP_ENTER     0x001035bc
#define GUEST_TIMER_WRAP_EXIT      0x1035c3
#define GUEST_CONTEXT_SWITCH_ENTER 0x00105a38
#define GUEST_CONTEXT_SWITCH_EXIT  0x105ae5
#define GUEST_SCHED_INIT_EXIT      0x10546d
#define GUEST_SCHEDULER_LOCK       0x00136468
#define GUEST_SCHEDULER_LOCKED(x)  (x)

#define GUEST_FORK_WINDOW_ENTER    0x103ef6
#define GUEST_THRFORK_WINDOW_ENTER 0x1041f1
#define GUEST_SLEEP_WINDOW_ENTER   0x103fe8
#define GUEST_VANISH_WINDOW_ENTER  0x104582
#define GUEST_READLINE_WINDOW_ENTER 0x00106fc9
#define GUEST_READLINE_WINDOW_EXIT 0x107245

#endif
