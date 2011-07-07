/**
 * @file kernel_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_KERNEL_SPECIFICS_H
#define __LS_KERNEL_SPECIFICS_H

struct ls_state;
struct sched_state;

/* Miscellaneous simple information */
int kern_get_current_tcb(struct ls_state *);
int kern_get_current_tid(struct ls_state *);
bool kern_timer_entering(struct ls_state *);
bool kern_timer_exiting(struct ls_state *);
bool kern_context_switch_entering(struct ls_state *);
bool kern_context_switch_exiting(struct ls_state *);
bool kern_sched_init_done(struct ls_state *);

/* Lifecycle */
bool kern_forking(struct ls_state *);
bool kern_vanishing(struct ls_state *);
bool kern_sleeping(struct ls_state *);
bool kern_thread_runnable(struct ls_state *, int *);
bool kern_thread_descheduling(struct ls_state *, int *);

/* Other / init */
int kern_get_init_tid(void);
void kern_init_runqueue(struct sched_state *,
			void (*)(struct sched_state *, int, bool));
bool kern_fork_returns_to_cs(void);

#endif
