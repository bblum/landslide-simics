/**
 * @file kernel_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_KERNEL_SPECIFICS_H
#define __LS_KERNEL_SPECIFICS_H

#include "landslide.h"

struct sched_state;

/* Miscellaneous simple information */
int kern_get_current_tcb(struct ls_state *);
int kern_get_current_tid(struct ls_state *);
int kern_timer_entering(struct ls_state *);
int kern_timer_exiting(struct ls_state *);

/* Lifecycle */
bool kern_thread_is_appearing(struct ls_state *);
int kern_thread_appearing(struct ls_state *);
bool kern_thread_is_disappearing(struct ls_state *);
int kern_thread_disappearing(struct ls_state *);

/* Other / init */
int kern_get_init_thread(void);
void kern_init_runqueue(struct sched_state *,
			void (*)(struct sched_state *, int));

#endif
