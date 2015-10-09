/**
 * @file user_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_USER_SPECIFICS_H
#define __LS_USER_SPECIFICS_H

#include <simics/api.h>

#include "student_specifics.h"

struct ls_state;

/* Userspace */

bool testing_userspace();
bool ignore_dr_function(unsigned int eip);
/* syscalls / misc */
bool user_report_end_fail(conf_object_t *cpu, unsigned int eip);
bool user_yielding(struct ls_state *ls);
bool user_make_runnable_entering(unsigned int eip);
bool user_sleep_entering(unsigned int eip);
/* malloc */
bool user_mm_init_entering(unsigned int eip);
bool user_mm_init_exiting(unsigned int eip);
bool user_mm_malloc_entering(conf_object_t *cpu, unsigned int eip, unsigned int *size);
bool user_mm_malloc_exiting(conf_object_t *cpu, unsigned int eip, unsigned int *base);
bool user_mm_free_entering(conf_object_t *cpu, unsigned int eip, unsigned int *base);
bool user_mm_free_exiting(unsigned int eip);
bool user_mm_realloc_entering(conf_object_t *cpu, unsigned int eip,
			      unsigned int *orig_base, unsigned int *size);
bool user_mm_realloc_exiting(conf_object_t *cpu, unsigned int eip, unsigned int *base);
bool user_locked_malloc_entering(unsigned int eip);
bool user_locked_malloc_exiting(unsigned int eip);
bool user_locked_free_entering(unsigned int eip);
bool user_locked_free_exiting(unsigned int eip);
bool user_locked_calloc_entering(unsigned int eip);
bool user_locked_calloc_exiting(unsigned int eip);
bool user_locked_realloc_entering(unsigned int eip);
bool user_locked_realloc_exiting(unsigned int eip);
/* elf regions */
bool user_address_in_heap(unsigned int addr);
bool user_address_global(unsigned int addr);
bool user_panicked(conf_object_t *cpu, unsigned int addr, char **buf);
/* thread lifecycle */
bool user_thr_init_entering(unsigned int eip);
bool user_thr_init_exiting(unsigned int eip);
bool user_thr_create_entering(unsigned int eip);
bool user_thr_create_exiting(unsigned int eip);
bool user_thr_join_entering(unsigned int eip);
bool user_thr_join_exiting(unsigned int eip);
bool user_thr_exit_entering(unsigned int eip);
/* mutexes */
bool user_mutex_init_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_mutex_init_exiting(unsigned int eip);
bool user_mutex_lock_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_mutex_lock_exiting(unsigned int eip);
bool user_mutex_trylock_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_mutex_trylock_exiting(conf_object_t *cpu, unsigned int eip, bool *succeeded);
bool user_mutex_unlock_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_mutex_unlock_exiting(unsigned int eip);
bool user_mutex_destroy_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_mutex_destroy_exiting(unsigned int eip);
/* cvars */
bool user_cond_wait_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_cond_wait_exiting(unsigned int eip);
bool user_cond_signal_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_cond_signal_exiting(unsigned int eip);
bool user_cond_broadcast_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_cond_broadcast_exiting(unsigned int eip);
/* sems */
bool user_sem_wait_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_sem_wait_exiting(unsigned int eip);
bool user_sem_signal_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_sem_signal_exiting(unsigned int eip);
/* rwlox */
bool user_rwlock_lock_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr, bool *write);
bool user_rwlock_lock_exiting(unsigned int eip);
bool user_rwlock_unlock_entering(conf_object_t *cpu, unsigned int eip, unsigned int *addr);
bool user_rwlock_unlock_exiting(unsigned int eip);

#endif
