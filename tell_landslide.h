/**
 * @file tell_landslide.h
 * @brief Specification for pebbles kernels to tell landslide about themselves.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_TELL_LANDSLIDE_H
#define __LS_TELL_LANDSLIDE_H

void tell_landslide_sched_init_done(void);
void tell_landslide_forking(void);
void tell_landslide_vanishing(void);
void tell_landslide_sleeping(void);
void tell_landslide_thread_runnable(int tid);
void tell_landslide_thread_descheduling(int tid);

/* Remember, you only need to use these if you have mutexes that leave blocked
 * threads on the runqueue. */
void tell_landslide_mutex_locking(void *mutex_addr);
void tell_landslide_mutex_blocking(void *mutex_addr, int owner_tid);
void tell_landslide_mutex_locking_done(void);
void tell_landslide_mutex_unlocking(void *mutex_addr);
void tell_landslide_mutex_unlocking_done(void);

#endif
