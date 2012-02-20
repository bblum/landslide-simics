/**
 * @file kernel_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_KERNEL_SPECIFICS_H
#define __LS_KERNEL_SPECIFICS_H

#include <simics/api.h>

#define GUEST_POBBLES_PARENT

#if defined(GUEST_POBBLES)
#include "kernel_specifics_pobbles.h"
#include "kernel_specifics_types.h"
#elif defined(GUEST_POBBLES_RACE)
#include "kernel_specifics_pobbles_race.h"
#include "kernel_specifics_types.h"
#elif defined(GUEST_POBBLES_PARENT)
#include "kernel_specifics_pobbles_parent.h"
#include "kernel_specifics_types.h"

// TODO: elsif ...
#endif

struct sched_state;

/* Miscellaneous simple information */
int kern_get_current_tcb(conf_object_t *cpu);
int kern_get_current_tid(conf_object_t *cpu);
bool kern_timer_entering(int eip);
bool kern_timer_exiting(int eip);
int kern_get_timer_wrap_begin();
bool kern_context_switch_entering(int eip);
bool kern_context_switch_exiting(int eip);
bool kern_sched_init_done(int eip);
bool kern_in_scheduler(int eip);
bool kern_access_in_scheduler(int addr);
bool kern_scheduler_locked(conf_object_t *cpu);
bool kern_mutex_ignore(int addr);
bool kern_panicked(conf_object_t *cpu, int eip, char **buf);

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
bool kern_address_own_kstack(conf_object_t *cpu, int addr);
bool kern_address_other_kstack(conf_object_t *, int addr, int chunk, int size,
			       int *tid);
void kern_address_hint(conf_object_t *, char *buf, int buflen,
		       int addr, int chunk, int size);

/* Other / init */
int kern_get_init_tid(void);
int kern_get_idle_tid(void);
int kern_get_shell_tid(void);
int kern_get_first_tid(void);
bool kern_has_idle(void);
void kern_init_runqueue(struct sched_state *,
			void (*)(struct sched_state *, int, bool));
bool kern_fork_returns_to_cs(void);
bool kern_fork_return_spot(int eip);
bool kern_current_extra_runnable(conf_object_t *cpu);

#endif
