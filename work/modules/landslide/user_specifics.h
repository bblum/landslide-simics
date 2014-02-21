/**
 * @file user_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_USER_SPECIFICS_H
#define __LS_USER_SPECIFICS_H

#include <simics/api.h>

#include "student_specifics.h"

/* Userspace */

bool testing_userspace();

bool user_mm_init_entering(int eip);
bool user_mm_init_exiting(int eip);
bool user_mm_malloc_entering(conf_object_t *cpu, int eip, int *size);
bool user_mm_malloc_exiting(conf_object_t *cpu, int eip, int *base);
bool user_mm_free_entering(conf_object_t *cpu, int eip, int *base);
bool user_mm_free_exiting(int eip);
bool user_address_in_heap(int addr);
bool user_address_global(int addr);
bool user_panicked(conf_object_t *cpu, int addr, char **buf);
bool user_thr_init_entering(int eip);
bool user_thr_init_exiting(int eip);
bool user_thr_create_entering(int eip);
bool user_thr_create_exiting(int eip);
bool user_thr_join_entering(int eip);
bool user_thr_join_exiting(int eip);
bool user_thr_exit_entering(int eip);
bool user_thr_exit_exiting(int eip);
bool user_mutex_lock_entering(conf_object_t *cpu, int eip, int *addr);
bool user_mutex_lock_exiting(int eip);
bool user_mutex_trylock_entering(conf_object_t *cpu, int eip, int *addr);
bool user_mutex_trylock_exiting(conf_object_t *cpu, int eip, bool *succeeded);
bool user_mutex_unlock_entering(conf_object_t *cpu, int eip, int *addr);
bool user_mutex_unlock_exiting(int eip);
bool user_cond_wait_entering(conf_object_t *cpu, int eip, int *addr);
bool user_cond_wait_exiting(int eip);
bool user_cond_signal_entering(conf_object_t *cpu, int eip, int *addr);
bool user_cond_signal_exiting(int eip);
bool user_cond_broadcast_entering(conf_object_t *cpu, int eip, int *addr);
bool user_cond_broadcast_exiting(int eip);
bool user_sem_wait_entering(conf_object_t *cpu, int eip, int *addr);
bool user_sem_wait_exiting(int eip);
bool user_sem_signal_entering(conf_object_t *cpu, int eip, int *addr);
bool user_sem_signal_exiting(int eip);
bool user_rwlock_lock_entering(conf_object_t *cpu, int eip, int *addr, bool *write);
bool user_rwlock_lock_exiting(int eip);
bool user_rwlock_unlock_entering(conf_object_t *cpu, int eip, int *addr);
bool user_rwlock_unlock_exiting(int eip);

#endif
