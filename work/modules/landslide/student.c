/**
 * @file student.c
 * @brief Guest-implementation-specific things landslide needs to know
 *        Implementation for the POBBLES kernel.
 *
 * @author Ben Blum, and you, the student :)
 */

#include <simics/api.h>

#define MODULE_NAME "student_glue"

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

/* Is the currently-running thread not on the runqueue, and is runnable
 * anyway? For kernels that keep the current thread on the runqueue, this
 * function should return false always. */
bool kern_current_extra_runnable(conf_object_t *cpu)
{
#ifdef CURRENT_THREAD_LIVES_ON_RQ
	assert(CURRENT_THREAD_LIVES_ON_RQ == 1 ||
	       CURRENT_THREAD_LIVES_ON_RQ == 0);
	return !(bool)CURRENT_THREAD_LIVES_ON_RQ;
#else
	STATIC_ASSERT(false && "CURRENT_THREAD_LIVES_ON_RQ config missing -- must implement in student.c manually.");
	return false;
#endif
}

bool kern_ready_for_timer_interrupt(conf_object_t *cpu)
{
#ifdef PREEMPT_ENABLE_FLAG
#ifndef PREEMPT_ENABLE_VALUE
	STATIC_ASSERT(false && "preempt flag but not value defined");
	return false;
#else
	return READ_MEMORY(cpu, (unsigned int)PREEMPT_ENABLE_FLAG) == PREEMPT_ENABLE_VALUE;
#endif
#else
	/* no preempt enable flag. assume the scheduler protects itself by
	 * disabling interrupts wholesale. */
	return true;
#endif
}

