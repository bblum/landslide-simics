/**
 * @file kernel_specifics.c
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#include "kernel_specifics.h"
#include "schedule.h" /* TODO: separate the struct part into schedule_type.h */
#include "x86.h"

/******************************************************************************
 * Miscellaneous information
 ******************************************************************************/

/* Returns the tcb/tid of the currently scheduled thread. */
int kern_get_current_tcb(conf_object_t *cpu)
{
	return SIM_read_phys_memory(cpu, GUEST_CURRENT_TCB, WORD_SIZE);
}
int kern_get_current_tid(conf_object_t *cpu)
{
	return TID_FROM_TCB(cpu, kern_get_current_tcb(cpu));
}

/* The boundaries of the timer handler wrapper. */
bool kern_timer_entering(int eip)
{
	return eip == GUEST_TIMER_WRAP_ENTER;
}
bool kern_timer_exiting(int eip)
{
	return eip == GUEST_TIMER_WRAP_EXIT;
}
int kern_get_timer_wrap_begin()
{
	return GUEST_TIMER_WRAP_ENTER;
}

/* the boundaries of the context switcher */
bool kern_context_switch_entering(int eip)
{
	return eip == GUEST_CONTEXT_SWITCH_ENTER;
}
bool kern_context_switch_exiting(int eip)
{
	return eip == GUEST_CONTEXT_SWITCH_EXIT;
}

bool kern_sched_init_done(int eip)
{
	return eip == GUEST_SCHED_INIT_EXIT;
}

bool kern_in_scheduler(int eip)
{
	static const int sched_funx[][2] = GUEST_SCHEDULER_FUNCTIONS;

	for (int i = 0; i < ARRAY_SIZE(sched_funx); i++) {
		/* The get_func_end returns the last instr, so be inclusive */
		if (eip >= sched_funx[i][0] && eip <= sched_funx[i][1])
			return true;
	}

	return false;
}

bool kern_access_in_scheduler(int addr)
{
	static const int sched_syms[][2] = GUEST_SCHEDULER_GLOBALS;

	for (int i = 0; i < ARRAY_SIZE(sched_syms); i++) {
		if (addr >= sched_syms[i][0] &&
		    addr < sched_syms[i][0] + sched_syms[i][1])
			return true;
	}

	return false;
}

/* Anything that would prevent timer interrupts from triggering context
 * switches */
bool kern_scheduler_locked(conf_object_t *cpu)
{
	int x = SIM_read_phys_memory(cpu, GUEST_SCHEDULER_LOCK, WORD_SIZE);
	return GUEST_SCHEDULER_LOCKED(x);
}

/* Various global mutexes which should be ignored */
bool kern_mutex_ignore(int addr)
{
	static const int ignores[] = GUEST_MUTEX_IGNORES;
	for (int i = 0; i < ARRAY_SIZE(ignores); i++) {
		if (addr == ignores[i])
			return true;
	}
	return false;
}

/******************************************************************************
 * Yielding mutexes
 ******************************************************************************/

/* If the kernel uses yielding mutexes, we need to explicitly keep track of when
 * threads are blocked on them. (If mutexes deschedule, it should be safe to
 * have all these functions just return false.)
 * A "race" may happen if we decide on a choice point between when this says
 * a mutex-owning thread "enables" a blocked thread and when the actual enabling
 * instruction is executed. Hence (as a small-hammer solution) we don't allow
 * choice points to happen inside mutex_{,un}lock. */

bool kern_mutex_locking(conf_object_t *cpu, int eip, int *mutex)
{
	if (eip == GUEST_MUTEX_LOCK_ENTER) {
		*mutex = READ_STACK(cpu, GUEST_MUTEX_LOCK_MUTEX_ARGNUM);
		return true;
	} else {
		return false;
	}
}

/* Is the thread becoming "disabled" because the mutex is owned? */
bool kern_mutex_blocking(conf_object_t *cpu, int eip, int *tid)
{
	if (eip == GUEST_MUTEX_BLOCKED) {
		/* First argument to yield. */
		*tid = READ_STACK(cpu, 0);
		return true;
	} else {
		return false;
	}
}

/* This one also tells if the thread is re-enabled. */
bool kern_mutex_locking_done(int eip)
{
	return eip == GUEST_MUTEX_LOCK_EXIT;
}

/* Need to re-read the mutex addr because of unlocking mutexes in any order. */
bool kern_mutex_unlocking(conf_object_t *cpu, int eip, int *mutex)
{
	if (eip == GUEST_MUTEX_UNLOCK_ENTER) {
		*mutex = READ_STACK(cpu, GUEST_MUTEX_UNLOCK_MUTEX_ARGNUM);
		return true;
	} else {
		return false;
	}
}

bool kern_mutex_unlocking_done(int eip)
{
	return eip == GUEST_MUTEX_UNLOCK_EXIT;
}

/******************************************************************************
 * Lifecycle
 ******************************************************************************/

/* How to tell if a thread's life is beginning or ending */
bool kern_forking(int eip)
{
	return (eip == GUEST_FORK_WINDOW_ENTER)
	    || (eip == GUEST_THRFORK_WINDOW_ENTER);
}
bool kern_sleeping(int eip)
{
	return eip == GUEST_SLEEP_WINDOW_ENTER;
}
bool kern_vanishing(int eip)
{
	return eip == GUEST_VANISH_WINDOW_ENTER;
}
bool kern_readline_enter(int eip)
{
	return eip == GUEST_READLINE_WINDOW_ENTER;
}
bool kern_readline_exit(int eip)
{
	return eip == GUEST_READLINE_WINDOW_EXIT;
}

/* How to tell if a new thread is appearing or disappearing on the runqueue. */
static bool thread_becoming_runnable(conf_object_t *cpu, int eip)
{
	return (eip == GUEST_Q_ADD)
	    && (READ_STACK(cpu, GUEST_Q_ADD_Q_ARGNUM) == GUEST_RQ_ADDR);
}
bool kern_thread_runnable(conf_object_t *cpu, int eip, int *tid)
{
	if (thread_becoming_runnable(cpu, eip)) {
		/* 0(%esp) points to the return address; get the arg above it */
		*tid = TID_FROM_TCB(cpu, READ_STACK(cpu,
						    GUEST_Q_ADD_TCB_ARGNUM));
		return true;
	} else {
		return false;
	}
}

static bool thread_is_descheduling(conf_object_t *cpu, int eip)
{
	return ((eip == GUEST_Q_REMOVE)
	     && (READ_STACK(cpu, GUEST_Q_REMOVE_Q_ARGNUM) == GUEST_RQ_ADDR))
	    || ((eip == GUEST_Q_POP_RETURN)
	     && (READ_STACK(cpu, GUEST_Q_POP_Q_ARGNUM) == GUEST_RQ_ADDR));
}
bool kern_thread_descheduling(conf_object_t *cpu, int eip, int *tid)
{
	if (thread_is_descheduling(cpu, eip)) {
		int tcb;
		if (eip == GUEST_Q_REMOVE) {
			/* at beginning of sch_queue_remove */
			tcb = READ_STACK(cpu, GUEST_Q_REMOVE_TCB_ARGNUM);
		} else {
			/* at end of sch_queue_pop; see prior assert */
			tcb = GET_CPU_ATTR(cpu, eax);
		}
		*tid = TID_FROM_TCB(cpu, tcb);
		return true;
	} else {
		return false;
	}
}

/******************************************************************************
 * LMM
 ******************************************************************************/

bool kern_lmm_alloc_entering(conf_object_t *cpu, int eip, int *size)
{
	if (eip == GUEST_LMM_ALLOC_ENTER) {
		*size = READ_STACK(cpu, GUEST_LMM_ALLOC_SIZE_ARGNUM);
		return true;
	} else if (eip == GUEST_LMM_ALLOC_GEN_ENTER) {
		*size = READ_STACK(cpu, GUEST_LMM_ALLOC_GEN_SIZE_ARGNUM);
		return true;
	} else {
		return false;
	}
}

bool kern_lmm_alloc_exiting(conf_object_t *cpu, int eip, int *base)
{
	if (eip == GUEST_LMM_ALLOC_EXIT || eip == GUEST_LMM_ALLOC_GEN_EXIT) {
		*base = GET_CPU_ATTR(cpu, eax);
		return true;
	} else {
		return false;
	}
}

bool kern_lmm_free_entering(conf_object_t *cpu, int eip, int *base, int *size)
{
	if (eip == GUEST_LMM_FREE_ENTER) {
		*base = READ_STACK(cpu, GUEST_LMM_FREE_BASE_ARGNUM);
		*size = READ_STACK(cpu, GUEST_LMM_FREE_SIZE_ARGNUM);
		return true;
	} else {
		return false;
	}
}

bool kern_lmm_free_exiting(int eip)
{
	return (eip == GUEST_LMM_FREE_EXIT);
}

bool kern_address_in_heap(int addr)
{
	return (addr >= GUEST_IMG_END && addr < USER_MEM_START);
}

bool kern_address_global(int addr)
{
	return ((addr >= GUEST_DATA_START && addr < GUEST_DATA_END) ||
		(addr >= GUEST_BSS_START && addr < GUEST_BSS_END));
}

bool kern_address_own_kstack(conf_object_t *cpu, int addr)
{
	int stack_bottom = STACK_FROM_TCB(kern_get_current_tcb(cpu));
	return (addr >= stack_bottom && addr < stack_bottom + GUEST_STACK_SIZE);
}

bool kern_address_other_kstack(conf_object_t *cpu, int addr, int chunk,
			       int size, int *tid)
{
	if (size == KERN_TCB_SIZE) {
		int stack_bottom = STACK_FROM_TCB(chunk);
		if (addr >= stack_bottom &&
		    addr < stack_bottom + GUEST_STACK_SIZE) {
			*tid = TID_FROM_TCB(cpu, chunk);
			return true;
		}
	}
	return false;
}

void kern_address_hint(conf_object_t *cpu, char *buf, int buflen, int addr,
		       int chunk, int size)
{
	if (size == KERN_TCB_SIZE) {
		snprintf(buf, buflen, "tcb%d->%s",
			 (int)TID_FROM_TCB(cpu, chunk),
			 kern_tcb_field_name(addr-chunk));
	} else if (size == KERN_PCB_SIZE) {
		snprintf(buf, buflen, "pcb%d->%s",
			 (int)PID_FROM_PCB(cpu, chunk),
			 kern_pcb_field_name(addr-chunk));
	} else {
		snprintf(buf, buflen, "0x%.8x", addr);
	}
}

/******************************************************************************
 * Other / Init
 ******************************************************************************/

int kern_get_init_tid()
{
	return 1;
}

int kern_get_idle_tid()
{
	assert(false && "POBBLES does not have an idle tid!");
}

/* the tid of the shell (OK to assume the first shell never exits). */
int kern_get_shell_tid()
{
	return 2;
}

/* Which thread runs first on kernel init? */
int kern_get_first_tid()
{
	return 1;
}

void kern_init_runqueue(struct sched_state *s,
			void (*add_thread)(struct sched_state *, int, bool))
{
	/* Only init runs first in POBBLES, but other kernels may have idle. In
	 * POBBLES, init is not context-switched to to begin with. */
	add_thread(s, kern_get_init_tid(), false);
}

/* Do newly forked children exit to userspace through the end of the
 * context-switcher? (POBBLES does not; it bypasses the end to return_zero.) */
bool kern_fork_returns_to_cs()
{
	return false;
}

bool kern_fork_return_spot(int eip)
{
	// If kern_fork_returns_to_cs, this function should always return false.
	return eip == GUEST_FORK_RETURN_SPOT;
}
