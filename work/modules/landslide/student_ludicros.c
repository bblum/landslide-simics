/**
 * @file student.c
 * @brief Guest-implementation-specific things landslide needs to know
 *        Implementation for the ludicros kernel.
 *
 * @author Ben Blum, and you, the student :)
 */

#include <assert.h>
#include <simics/api.h>

#define MODULE_NAME "ludicros"

#include "common.h"
#include "kernel_specifics.h"
#include "schedule.h"
#include "x86.h"

/* List of primitives to help you implement these functions:
 * READ_MEMORY(cpu, addr) - Returns 4 bytes from kernel memory at addr
 * READ_BYTE(cpu, addr)   - Returns 1 byte from kernel memory at addr
 * GET_CPU_ATTR(cpu, eax) - Returns the value of a CPU register (esp, eip, ...)
 * GET_ESP0(cpu)          - Returns the current value of esp0
 */

void kern_init_threads(struct sched_state *s,
                       void (*add_thread)(struct sched_state *, int tid,
                                          bool on_runqueue))
{
	add_thread(s, kern_get_init_tid(), false);
	add_thread(s, kern_get_idle_tid(), false);
}

/* Is the currently-running thread not on the runqueue, and is runnable
 * anyway? For kernels that keep the current thread on the runqueue, this
 * function should return false always. */
#define GUEST_TCB_STATE_FLAG_OFFSET 20
#define GUEST_TCB_T_SIZE 76
bool kern_current_extra_runnable(conf_object_t *cpu)
{
	int esp0 = GET_ESP0(cpu);
	// from sched_find_tcb_by_ksp
	int tcb = ((esp0 & ~(PAGE_SIZE - 1)) + PAGE_SIZE) - GUEST_TCB_T_SIZE;

	int state_flag = READ_MEMORY(cpu, tcb + GUEST_TCB_STATE_FLAG_OFFSET);
	return state_flag != 1; // SCHED_NOT_RUNNABLE
}

/* Anything that would prevent timer interrupts from triggering context
 * switches */
bool kern_scheduler_locked(conf_object_t *cpu)
{
	return false;
}

