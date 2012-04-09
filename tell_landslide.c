/**
 * @file tell_landslide.c
 * @brief Specification for pebbles kernels to tell landslide about themselves.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include "tell_landslide.h"

void tell_landslide_decide(void) { }
void tell_landslide_thread_switch(int new_tid) { }
void tell_landslide_sched_init_done(void) { }
void tell_landslide_forking(void) { }
void tell_landslide_vanishing(void) { }
void tell_landslide_sleeping(void) { }
void tell_landslide_thread_on_rq(int tid) { }
void tell_landslide_thread_off_rq(int tid) { }
void tell_landslide_mutex_locking(void *mutex_addr) { }
void tell_landslide_mutex_blocking(int owner_tid) { }
void tell_landslide_mutex_locking_done(void) { }
void tell_landslide_mutex_unlocking(void *mutex_addr) { }
void tell_landslide_mutex_unlocking_done(void) { }
