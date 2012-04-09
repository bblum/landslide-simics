/**
 * @file kernel_specifics.c
 * @brief Guest-dependent-but-agnostic things landslide needs to know
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#define MODULE_NAME "glue"

#include "common.h"
#include "kernel_specifics.h"
#include "schedule.h" /* TODO: separate the struct part into schedule_type.h */
#include "x86.h"

/******************************************************************************
 * Miscellaneous information
 ******************************************************************************/

bool kern_decision_point(int eip)
{
	return eip == TELL_LANDSLIDE_DECIDE;
}

bool kern_thread_switch(conf_object_t *cpu, int eip, int *new_tid)
{
	if (eip == TELL_LANDSLIDE_THREAD_SWITCH) {
		*new_tid = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
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
	return eip == TELL_LANDSLIDE_SCHED_INIT_DONE;
}

bool kern_in_scheduler(conf_object_t *cpu, int eip)
{
	static const int sched_funx[][2] = GUEST_SCHEDULER_FUNCTIONS;

	for (int i = 0; i < ARRAY_SIZE(sched_funx); i++) {
		/* The get_func_end returns the last instr, so be inclusive */
		/* Don't use within_function here - huge performance regression.
		 * if (within_function(cpu, eip, sched_funx[i][0], sched_funx[i][1])) */
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

bool kern_within_functions(conf_object_t *cpu, int eip)
{
	/* The array is: { start_addr, end_addr, within ? 1 : 0 }.
	 * Later ones take precedence, so all of them have to be compared. */
	static const int within_functions[][3] = GUEST_WITHIN_FUNCTIONS;

	/* If there are no within_functions, the default answer is yes.
	 * Otherwise the default answer is no. */
	bool any_withins = false;
	bool answer = true;

	for (int i = 0; i < ARRAY_SIZE(within_functions); i++) {
		if (within_functions[i][2] == 1) {
			/* Must be within this function to allow. */
			if (!any_withins) {
				any_withins = true;
				answer = false;
			}
			if (within_function(cpu, eip, within_functions[i][0],
					    within_functions[i][1])) {
				answer = true;
			}
		} else {
			if (within_function(cpu, eip, within_functions[i][0],
					    within_functions[i][1])) {
				answer = false;
			}
		}
	}
	return answer;
}

#define GUEST_ASSERT_MSG "%s:%u: failed assertion `%s'"

bool kern_panicked(conf_object_t *cpu, int eip, char **buf)
{
	if (eip == GUEST_PANIC) {
		*buf = read_string(cpu, READ_STACK(cpu, 1));
		/* Can't call out to snprintf in the general case because it
		 * would need repeated calls to read_string, and would basically
		 * need to be reimplemented entirely. Instead, special-case. */
		if (strcmp(*buf, GUEST_ASSERT_MSG) == 0) {
			char *file_str   = read_string(cpu, READ_STACK(cpu, 2));
			int line         = READ_STACK(cpu, 3);
			char *assert_msg = read_string(cpu, READ_STACK(cpu, 4));
			/* 12 is enough space for any stringified int. This will
			 * allocate a little extra, but we don't care. */
			int length = strlen(GUEST_ASSERT_MSG) + strlen(file_str)
			             + strlen(assert_msg) + 12;
			MM_FREE(*buf);
			*buf = MM_XMALLOC(length, char);
			snprintf(*buf, length, GUEST_ASSERT_MSG, file_str, line,
				 assert_msg);
			MM_FREE(file_str);
			MM_FREE(assert_msg);
		}
		return true;
	} else {
		return false;
	}
}

bool kern_kernel_main(int eip)
{
	return eip == GUEST_KERNEL_MAIN;
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
	if (eip == TELL_LANDSLIDE_MUTEX_LOCKING) {
		*mutex = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

/* Is the thread becoming "disabled" because the mutex is owned? */
bool kern_mutex_blocking(conf_object_t *cpu, int eip, int *owner_tid)
{
	if (eip == TELL_LANDSLIDE_MUTEX_BLOCKING) {
		*owner_tid = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

/* This one also tells if the thread is re-enabled. */
bool kern_mutex_locking_done(int eip)
{
	return eip == TELL_LANDSLIDE_MUTEX_LOCKING_DONE;
}

/* Need to re-read the mutex addr because of unlocking mutexes in any order. */
bool kern_mutex_unlocking(conf_object_t *cpu, int eip, int *mutex)
{
	if (eip == TELL_LANDSLIDE_MUTEX_UNLOCKING) {
		*mutex = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

bool kern_mutex_unlocking_done(int eip)
{
	return eip == TELL_LANDSLIDE_MUTEX_UNLOCKING_DONE;
}

/******************************************************************************
 * Lifecycle
 ******************************************************************************/

/* How to tell if a thread's life is beginning or ending */
bool kern_forking(int eip)
{
	return eip == TELL_LANDSLIDE_FORKING;
}
bool kern_sleeping(int eip)
{
	return eip == TELL_LANDSLIDE_SLEEPING;
}
bool kern_vanishing(int eip)
{
	return eip == TELL_LANDSLIDE_VANISHING;
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
bool kern_thread_runnable(conf_object_t *cpu, int eip, int *tid)
{
	if (eip == TELL_LANDSLIDE_THREAD_RUNNABLE) {
		/* 0(%esp) points to the return address; get the arg above it */
		*tid = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

bool kern_thread_descheduling(conf_object_t *cpu, int eip, int *tid)
{
	if (eip == TELL_LANDSLIDE_THREAD_DESCHEDULING) {
		*tid = READ_STACK(cpu, 1);
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

#if 0
bool kern_address_own_kstack(conf_object_t *cpu, int addr)
{
	int stack_bottom = STACK_FROM_TCB(kern_get_current_tcb(cpu));
	return (addr >= stack_bottom && addr < stack_bottom + GUEST_STACK_SIZE);
}

bool kern_address_other_kstack(conf_object_t *cpu, int addr, int chunk,
			       int size, int *tid)
{
	if (size == GUEST_TCB_CHUNK_SIZE) {
		//int stack_bottom = STACK_FROM_TCB(chunk+GUEST_TCB_OFFSET);
		int stack_bottom = chunk;
		if (addr >= stack_bottom &&
		    addr < stack_bottom + GUEST_STACK_SIZE) {
			*tid = TID_FROM_TCB(cpu, chunk + GUEST_TCB_OFFSET);
			return true;
		}
	}
	return false;
}

static const char *member_name(const struct struct_member *members,
			       int num_members, int offset)
{
	for (int i = 0; i < num_members; i++) {
		if (offset >= members[i].offset &&
		    offset < members[i].offset + members[i].size) {
			return members[i].name;
		}
	}
	return "((unknown))";
}

void kern_address_hint(conf_object_t *cpu, char *buf, int buflen, int addr,
		       int chunk, int size)
{
	if (size == GUEST_TCB_CHUNK_SIZE) {
		snprintf(buf, buflen, "tcb%d->%s",
			 (int)TID_FROM_TCB(cpu, chunk + GUEST_TCB_OFFSET),
			 member_name(guest_tcb_t, ARRAY_SIZE(guest_tcb_t),
		                     addr - chunk));
	} else if (size == GUEST_PCB_T_SIZE) {
		snprintf(buf, buflen, "pcb%d->%s",
			 (int)PID_FROM_PCB(cpu, chunk),
			 member_name(guest_pcb_t, ARRAY_SIZE(guest_pcb_t),
		                     addr - chunk));
	} else {
		snprintf(buf, buflen, "0x%.8x", addr);
	}
}
#endif

/******************************************************************************
 * Other / Init
 ******************************************************************************/

int kern_get_init_tid()
{
	return GUEST_INIT_TID;
}

int kern_get_idle_tid()
{
#ifdef GUEST_IDLE_TID
	return GUEST_IDLE_TID;
#else
	assert(false && "This kernel does not have an idle tid!");
#endif
}

/* the tid of the shell (OK to assume the first shell never exits). */
int kern_get_shell_tid()
{
	return GUEST_SHELL_TID;
}

/* Which thread runs first on kernel init? */
int kern_get_first_tid()
{
	return GUEST_FIRST_TID;
}

/* Is there an idle thread that runs when nobody else is around? */
bool kern_has_idle()
{
#ifdef GUEST_IDLE_TID
	return true;
#else
	return false;
#endif
}

void kern_init_threads(struct sched_state *s,
                       void (*add_thread)(struct sched_state *, int tid,
                                          bool on_runqueue))
{
	GUEST_STARTING_THREAD_CODE;
}
