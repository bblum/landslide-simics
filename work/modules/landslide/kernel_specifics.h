/**
 * @file kernel_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_KERNEL_SPECIFICS_H
#define __LS_KERNEL_SPECIFICS_H

#include <simics/api.h>

#include "student_specifics.h"

struct sched_state;

/* Miscellaneous simple information */
bool kern_decision_point(int eip);
bool kern_thread_switch(conf_object_t *cpu, int eip, int *new_tid);
bool kern_timer_entering(int eip);
bool kern_timer_exiting(int eip);
int kern_get_timer_wrap_begin(void);
bool kern_context_switch_entering(int eip);
bool kern_context_switch_exiting(int eip);
bool kern_sched_init_done(int eip);
bool kern_in_scheduler(conf_object_t *cpu, int eip);
bool kern_access_in_scheduler(int addr);
bool kern_ready_for_timer_interrupt(conf_object_t *cpu);
bool kern_within_functions(conf_object_t *cpu, int eip);
bool kern_panicked(conf_object_t *cpu, int eip, char **buf);
bool kern_kernel_main(int eip);

/* Yielding-mutex interactions. */
bool kern_mutex_locking(conf_object_t *cpu, int eip, int *mutex);
bool kern_mutex_blocking(conf_object_t *cpu, int eip, int *tid);
bool kern_mutex_locking_done(int eip);
bool kern_mutex_unlocking(conf_object_t *cpu, int eip, int *mutex);
bool kern_mutex_unlocking_done(int eip);

/* Lifecycle */
bool kern_forking(int eip);
bool kern_vanishing(int eip);
bool kern_sleeping(int eip);
bool kern_readline_enter(int eip);
bool kern_readline_exit(int eip);
bool kern_thread_runnable(conf_object_t *cpu, int eip, int *);
bool kern_thread_descheduling(conf_object_t *cpu, int eip, int *);

/* Dynamic memory allocation */
bool kern_lmm_alloc_entering(conf_object_t *cpu, int eip, int *size);
bool kern_lmm_alloc_exiting(conf_object_t *cpu, int eip, int *base);
bool kern_lmm_free_entering(conf_object_t *cpu, int eip, int *base, int *size);
bool kern_lmm_free_exiting(int eip);

/* Other memory operations */
bool kern_address_in_heap(int addr);
bool kern_address_global(int addr);
/* The following three are optional; you don't need to worry about them :) */
#define STUDENT_FRIENDLY
#ifndef STUDENT_FRIENDLY
bool kern_address_own_kstack(conf_object_t *cpu, int addr);
bool kern_address_other_kstack(conf_object_t *, int addr, int chunk, int size,
			       int *tid);
void kern_address_hint(conf_object_t *, char *buf, int buflen,
		       int addr, int chunk, int size);
#endif

/* Other / init */
int kern_get_init_tid(void);
int kern_get_idle_tid(void);
int kern_get_shell_tid(void);
int kern_get_first_tid(void);
bool kern_has_idle(void);
void kern_init_threads(struct sched_state *,
		       void (*)(struct sched_state *, int, bool));
bool kern_current_extra_runnable(conf_object_t *cpu);

#endif
