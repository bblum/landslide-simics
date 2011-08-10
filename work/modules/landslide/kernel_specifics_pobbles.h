/**
 * @file kernel_specifics_pobbles.h
 * @brief #defines for the pobbles guest kernel
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_KERNEL_SPECIFICS_POBBLES_H
#define __LS_KERNEL_SPECIFICS_POBBLES_H

/* Where to find the currently running TCB pointer */
#define GUEST_CURRENT_TCB 0x00152420
#define GUEST_TCB_TID_OFFSET 0
#define TID_FROM_TCB(ls, tcb) \
	SIM_read_phys_memory(ls->cpu0, tcb + GUEST_TCB_TID_OFFSET, WORD_SIZE)

#define GUEST_RQ_ADDR 0x00152424

/* Where is it that runnable threads appear and disappear? */
#define GUEST_Q_ADD                0x00105613
#define GUEST_Q_ADD_Q_ARGNUM       1 /* rq to modify is first argument */
#define GUEST_Q_ADD_TCB_ARGNUM     2 /* target tcb is second argument */
#define GUEST_Q_REMOVE             0x001056d5
#define GUEST_Q_REMOVE_Q_ARGNUM    1 /* same as above */
#define GUEST_Q_REMOVE_TCB_ARGNUM  2 /* same as above */
#define GUEST_Q_POP_RETURN         0x001056d4 /* thread in eax; discus */
#define GUEST_Q_POP_Q_ARGNUM       1
/* XXX: this is discus; for better interface maybe make an enum type for these
 * functions wherein one takes the thread as an arg and others return it...
 * a challenge: use symtab to extract either the start or the end of a fn */

/* Interrupt handler information */
#define GUEST_TIMER_WRAP_ENTER     0x001035bc
#define GUEST_TIMER_WRAP_EXIT      0x001035c3 /* should always be "iret" */
/* When does a context switch happen */
#define GUEST_CONTEXT_SWITCH_ENTER 0x001059aa
#define GUEST_CONTEXT_SWITCH_EXIT  0x00105a57
/* When is it safe to assume init/idle are initialised? */
#define GUEST_SCHED_INIT_EXIT      0x001053ed

/* The address of the scheduler locked flag */
#define GUEST_SCHEDULER_LOCK       0x001323e0
#define GUEST_SCHEDULER_LOCKED(x)  (x)

/* Windows around the lifecycle-changing parts of fork and vanish, for
 * determining when a thread's life begins and ends. The fork window should
 * include only the instructions which cause the child thread to be added to
 * the runqueue, and the vanish window should include only the instructions
 * which cause the final removal of the vanishing thread from the runqueue.
 * NOTE: A kernel may have more points than these; this is not generalised. */
#define GUEST_FORK_WINDOW_ENTER    0x00103ec0
#define GUEST_THRFORK_WINDOW_ENTER 0x001041bb
#define GUEST_SLEEP_WINDOW_ENTER   0x00103fb2
#define GUEST_VANISH_WINDOW_ENTER  0x0010450d

#endif
