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
struct ls_state;

/* Miscellaneous simple information */
bool kern_decision_point(unsigned int eip);
bool kern_thread_switch(conf_object_t *cpu, unsigned int eip, unsigned int *new_tid);
bool kern_timer_entering(unsigned int eip);
bool kern_timer_exiting(unsigned int eip);
int kern_get_timer_wrap_begin(void);
bool kern_context_switch_entering(unsigned int eip);
bool kern_context_switch_exiting(unsigned int eip);
bool kern_sched_init_done(unsigned int eip);
bool kern_in_scheduler(conf_object_t *cpu, unsigned int eip);
bool kern_access_in_scheduler(unsigned int addr);
bool kern_ready_for_timer_interrupt(conf_object_t *cpu);
bool kern_within_functions(struct ls_state *ls);
bool _within_functions(struct ls_state *ls, const int within_functions[][3], unsigned int length);
void read_panic_message(conf_object_t *cpu, unsigned int eip, char **buf);
bool kern_panicked(conf_object_t *cpu, unsigned int eip, char **buf);
bool kern_page_fault_handler_entering(unsigned int eip);
bool kern_killed_faulting_user_thread(conf_object_t *cpu, unsigned int eip);
bool kern_kernel_main(unsigned int eip);

/* Yielding-mutex interactions. */
bool kern_mutex_locking(conf_object_t *cpu, unsigned int eip, unsigned int *mutex);
bool kern_mutex_blocking(conf_object_t *cpu, unsigned int eip, unsigned int *tid);
bool kern_mutex_locking_done(conf_object_t *cpu, unsigned int eip, unsigned int *mutex);
bool kern_mutex_unlocking(conf_object_t *cpu, unsigned int eip, unsigned int *mutex);
bool kern_mutex_unlocking_done(unsigned int eip);
bool kern_mutex_trylocking(conf_object_t *cpu, unsigned int eip, unsigned int *mutex);
bool kern_mutex_trylocking_done(conf_object_t *cpu, unsigned int eip, unsigned int *mutex, bool *success);

/* Lifecycle */
bool kern_forking(unsigned int eip);
bool kern_vanishing(unsigned int eip);
bool kern_sleeping(unsigned int eip);
bool kern_readline_enter(unsigned int eip);
bool kern_readline_exit(unsigned int eip);
bool kern_exec_enter(unsigned int eip);
bool kern_thread_runnable(conf_object_t *cpu, unsigned int eip, unsigned int *);
bool kern_thread_descheduling(conf_object_t *cpu, unsigned int eip, unsigned int *);
bool kern_beginning_vanish_before_unreg_process(unsigned int eip);

/* Dynamic memory allocation */
bool kern_lmm_alloc_entering(conf_object_t *cpu, unsigned int eip, unsigned int *size);
bool kern_lmm_alloc_exiting(conf_object_t *cpu, unsigned int eip, unsigned int *base);
bool kern_lmm_free_entering(conf_object_t *cpu, unsigned int eip, unsigned int *base, unsigned int *size);
bool kern_lmm_free_exiting(unsigned int eip);
bool kern_lmm_remove_free_entering(unsigned int eip);
bool kern_lmm_remove_free_exiting(unsigned int eip);

/* Other memory operations */
bool kern_address_in_heap(unsigned int addr);
bool kern_address_global(unsigned int addr);

/* Other / init */
int kern_get_init_tid(void);
int kern_get_idle_tid(void);
int kern_get_shell_tid(void);
int kern_get_first_tid(void);
bool kern_has_idle(void);
void kern_init_threads(struct sched_state *,
		       void (*)(struct sched_state *, unsigned int, bool));
bool kern_current_extra_runnable(conf_object_t *cpu);
bool kern_wants_us_to_dump_stack(unsigned int eip);
bool kern_vm_user_copy_enter(unsigned int eip);
bool kern_vm_user_copy_exit(unsigned int eip);

#endif
