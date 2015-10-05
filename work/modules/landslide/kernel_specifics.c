/**
 * @file kernel_specifics.c
 * @brief Guest-dependent-but-agnostic things landslide needs to know
 * @author Ben Blum
 */

#include <simics/api.h>

#define MODULE_NAME "kernel glue"

#include "common.h"
#include "kernel_specifics.h"
#include "kspec.h"
#include "stack.h"
#include "x86.h"

/******************************************************************************
 * Miscellaneous information
 ******************************************************************************/

bool kern_decision_point(unsigned int eip)
{
	return eip == TELL_LANDSLIDE_DECIDE;
}

bool kern_thread_switch(conf_object_t *cpu, unsigned int eip, unsigned int *new_tid)
{
	if (eip == TELL_LANDSLIDE_THREAD_SWITCH) {
		*new_tid = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

/* The boundaries of the timer handler wrapper. */
bool kern_timer_entering(unsigned int eip)
{
	return eip == GUEST_TIMER_WRAP_ENTER;
}
bool kern_timer_exiting(unsigned int eip)
{
	return eip == GUEST_TIMER_WRAP_EXIT;
}
int kern_get_timer_wrap_begin()
{
	return GUEST_TIMER_WRAP_ENTER;
}

/* the boundaries of the context switcher */
bool kern_context_switch_entering(unsigned int eip)
{
	return eip == GUEST_CONTEXT_SWITCH_ENTER
#ifdef GUEST_CONTEXT_SWITCH_ENTER2
		|| eip == GUEST_CONTEXT_SWITCH_ENTER2
#endif
		;
}
bool kern_context_switch_exiting(unsigned int eip)
{
	return eip == GUEST_CONTEXT_SWITCH_EXIT
#ifdef GUEST_CONTEXT_SWITCH_EXIT0
		|| eip == GUEST_CONTEXT_SWITCH_EXIT0
#endif
#ifdef GUEST_CONTEXT_SWITCH_EXIT2
		|| eip == GUEST_CONTEXT_SWITCH_EXIT2
#endif
		;
}

bool kern_sched_init_done(unsigned int eip)
{
	return eip == TELL_LANDSLIDE_SCHED_INIT_DONE;
}

bool kern_in_scheduler(conf_object_t *cpu, unsigned int eip)
{
	static const unsigned int sched_funx[][2] = GUEST_SCHEDULER_FUNCTIONS;

	for (int i = 0; i < ARRAY_SIZE(sched_funx); i++) {
		/* The get_func_end returns the last instr, so be inclusive */
		/* Don't use within_function here - huge performance regression.
		 * if (within_function(cpu, eip, sched_funx[i][0], sched_funx[i][1])) */
		if (eip >= sched_funx[i][0] && eip <= sched_funx[i][1])
			return true;
	}

	return false;
}

bool kern_access_in_scheduler(unsigned int addr)
{
	static const unsigned int sched_syms[][2] = GUEST_SCHEDULER_GLOBALS;

	for (int i = 0; i < ARRAY_SIZE(sched_syms); i++) {
		if (addr >= sched_syms[i][0] &&
		    addr < sched_syms[i][0] + sched_syms[i][1])
			return true;
	}

	return false;
}

bool _within_functions(struct ls_state *ls, const unsigned int within_functions[][3], unsigned int length)
{
	/* The array is: { start_addr, end_addr, within ? 1 : 0 }.
	 * Later ones take precedence, so all of them have to be compared. */

	/* If there are no within_functions, the default answer is yes.
	 * Otherwise the default answer is no. */
	bool any_withins = false;
	bool answer = true;

	struct stack_trace *st = stack_trace(ls);

	for (int i = 0; i < length; i++) {
		if (within_functions[i][2] == 1) {
			/* Must be within this function to allow. */
			if (!any_withins) {
				any_withins = true;
				answer = false;
			}
			if (within_function_st(st, within_functions[i][0],
					    within_functions[i][1])) {
				answer = true;
			}
		} else {
			if (within_function_st(st, within_functions[i][0],
					    within_functions[i][1])) {
				answer = false;
			}
		}
	}

	free_stack_trace(st);
	return answer;
}

bool kern_within_functions(struct ls_state *ls)
{
	static const unsigned int within_functions[][3] = KERN_WITHIN_FUNCTIONS;
	int length = ARRAY_SIZE(within_functions);
	return _within_functions(ls, within_functions, length);
}

#define MK_DISK_IO_FN(name, index)					\
	bool name(unsigned int eip) {					\
		static const unsigned int disk_io_fns[][2] = DISK_IO_FNS; \
		for (int i = 0; i < ARRAY_SIZE(disk_io_fns); i++) {	\
			if (eip == disk_io_fns[i][index]) return true;	\
		}							\
		return false;						\
	}
MK_DISK_IO_FN(kern_enter_disk_io_fn, 0);
MK_DISK_IO_FN(kern_exit_disk_io_fn,  1);

#ifdef PINTOS_KERNEL
#define PANIC_ASSERT_MSG "assertion `%s' failed."
#define PANIC_ASSERT_TEMPLATE "%s:%u: assertion `%s' failed."
#define PANIC_MSG_ARGNUM 4
#define PANIC_FILE_ARGNUM 1
#define PANIC_LINE_ARGNUM 2
#define PANIC_ASSERT_MSG_ARGNUM 5
#else
#define PANIC_ASSERT_MSG "%s:%u: failed assertion `%s'"
#define PANIC_ASSERT_TEMPLATE PANIC_ASSERT_MSG
#define PANIC_MSG_ARGNUM 1
#define PANIC_FILE_ARGNUM 2
#define PANIC_LINE_ARGNUM 3
#define PANIC_ASSERT_MSG_ARGNUM 4
#endif

void read_panic_message(conf_object_t *cpu, unsigned int eip, char **buf)
{
#ifdef USER_PANIC_ENTER
	assert(eip == GUEST_PANIC || eip == USER_PANIC_ENTER);
#else
	assert(eip == GUEST_PANIC);
#endif

	*buf = read_string(cpu, READ_STACK(cpu, PANIC_MSG_ARGNUM));
	/* Can't call out to scnprintf in the general case because it
	 * would need repeated calls to read_string, and would basically
	 * need to be reimplemented entirely. Instead, special-case. */
	if (strcmp(*buf, PANIC_ASSERT_MSG) == 0) {
		char *file_str   = read_string(cpu, READ_STACK(cpu, PANIC_FILE_ARGNUM));
		int line         = READ_STACK(cpu, PANIC_LINE_ARGNUM);
		char *assert_msg = read_string(cpu, READ_STACK(cpu, PANIC_ASSERT_MSG_ARGNUM));
		/* 12 is enough space for any stringified int. This will
		 * allocate a little extra, but we don't care. */
		int length = strlen(PANIC_ASSERT_TEMPLATE) + strlen(file_str)
			     + strlen(assert_msg) + 12;
		MM_FREE(*buf);
		*buf = MM_XMALLOC(length, char);
		scnprintf(*buf, length, PANIC_ASSERT_TEMPLATE, file_str, line,
			  assert_msg);
		MM_FREE(file_str);
		MM_FREE(assert_msg);
	}
}

bool kern_panicked(conf_object_t *cpu, unsigned int eip, char **buf)
{
	if (eip == GUEST_PANIC) {
		read_panic_message(cpu, eip, buf);
		return true;
	} else {
		return false;
	}
}

bool kern_page_fault_handler_entering(unsigned int eip)
{
#ifdef GUEST_PF_HANDLER
	return eip == GUEST_PF_HANDLER;
#else
	return false;
#endif
}

bool kern_killed_faulting_user_thread(conf_object_t *cpu, unsigned int eip)
{
#ifdef GUEST_THREAD_KILLED
	return eip == GUEST_THREAD_KILLED
#ifdef GUEST_THREAD_KILLED_ARG
		&& READ_STACK(cpu, 1) == GUEST_THREAD_KILLED_ARG
#endif
		;
#else
	return false;
#endif
}

bool kern_kernel_main(unsigned int eip)
{
	return eip == GUEST_KERNEL_MAIN;
}

/******************************************************************************
 * Yielding mutexes
 ******************************************************************************/

bool kern_mutex_initing(conf_object_t *cpu, unsigned int eip,
			unsigned int *mutex, bool *isnt_mutex)
{
#ifdef PINTOS_KERNEL
	if (eip == GUEST_SEMA_INIT_ENTER) {
		*mutex = READ_STACK(cpu, GUEST_SEMA_INIT_SEMA_ARGNUM);
		*isnt_mutex = 1 != READ_STACK(cpu, GUEST_SEMA_INIT_VALUE_ARGNUM);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}

/* If the kernel uses yielding mutexes, we need to explicitly keep track of when
 * threads are blocked on them. (If mutexes deschedule, it should be safe to
 * have all these functions just return false.)
 * A "race" may happen if we decide on a choice point between when this says
 * a mutex-owning thread "enables" a blocked thread and when the actual enabling
 * instruction is executed. Hence (as a small-hammer solution) we don't allow
 * choice points to happen inside mutex_{,un}lock. */

bool kern_mutex_locking(conf_object_t *cpu, unsigned int eip, unsigned int *mutex)
{
#ifdef PINTOS_KERNEL
	if (eip == GUEST_SEMA_DOWN_ENTER) {
		*mutex = READ_STACK(cpu, GUEST_SEMA_DOWN_ARGNUM);
#else
	if (eip == TELL_LANDSLIDE_MUTEX_LOCKING) {
		*mutex = READ_STACK(cpu, 1);
#endif
		return true;
	} else {
		return false;
	}
}

/* Is the thread becoming "disabled" because the mutex is owned? */
bool kern_mutex_blocking(conf_object_t *cpu, unsigned int eip, unsigned int *owner_tid)
{
#ifdef PINTOS_KERNEL
	if (false) {
#else
	if (eip == TELL_LANDSLIDE_MUTEX_BLOCKING) {
		*owner_tid = READ_STACK(cpu, 1);
#endif
		return true;
	} else {
		return false;
	}
}

/* This one also tells if the thread is re-enabled. */
bool kern_mutex_locking_done(conf_object_t *cpu, unsigned int eip, unsigned int *mutex)
{
#ifdef PINTOS_KERNEL
	if (eip == GUEST_SEMA_DOWN_EXIT) {
		// FIXME: Ugly. Relies on compiler not changing the argument
		// packet above %ebp during the function's execution. In this
		// case, the value will not be used (see callsite), but...
		*mutex = READ_STACK(cpu, GUEST_SEMA_DOWN_ARGNUM);
#else
	if (eip == TELL_LANDSLIDE_MUTEX_LOCKING_DONE) {
		*mutex = READ_STACK(cpu, 1);
#endif
		return true;
	} else {
		return false;
	}
}

/* Need to re-read the mutex addr because of unlocking mutexes in any order. */
bool kern_mutex_unlocking(conf_object_t *cpu, unsigned int eip, unsigned int *mutex)
{
#ifdef PINTOS_KERNEL
	if (eip == GUEST_SEMA_UP_ENTER) {
		*mutex = READ_STACK(cpu, GUEST_SEMA_UP_ARGNUM);
#else
	if (eip == TELL_LANDSLIDE_MUTEX_UNLOCKING) {
		*mutex = READ_STACK(cpu, 1);
#endif
		return true;
	} else {
		return false;
	}
}

bool kern_mutex_unlocking_done(unsigned int eip)
{
#ifdef PINTOS_KERNEL
	return eip == GUEST_SEMA_UP_EXIT;
#else
	return eip == TELL_LANDSLIDE_MUTEX_UNLOCKING_DONE;
#endif
}

bool kern_mutex_trylocking(conf_object_t *cpu, unsigned int eip, unsigned int *mutex)
{
#ifdef PINTOS_KERNEL
	if (eip == GUEST_SEMA_TRY_DOWN_ENTER) {
		*mutex = READ_STACK(cpu, GUEST_SEMA_TRY_DOWN_ARGNUM);
#else
	if (eip == TELL_LANDSLIDE_MUTEX_TRYLOCKING) {
		*mutex = READ_STACK(cpu, 1);
#endif
		return true;
	} else {
		return false;
	}
}

bool kern_mutex_trylocking_done(conf_object_t *cpu, unsigned int eip, unsigned int *mutex, bool *success)
{
#ifdef PINTOS_KERNEL
	if (eip == GUEST_SEMA_TRY_DOWN_EXIT) {
		// FIXME: As previous FIXME above.
		// ..., but here the value needs to be used to unwind the
		// lockset tracking. So we really need gcc to not hose us here!
		*mutex = READ_STACK(cpu, GUEST_SEMA_TRY_DOWN_ARGNUM);
		*success = GET_CPU_ATTR(cpu, eax) != GUEST_SEMA_TRY_DOWN_FAILURE;
#else
	if (eip == TELL_LANDSLIDE_MUTEX_TRYLOCKING_DONE) {
		*mutex = READ_STACK(cpu, 1);
		*success = READ_STACK(cpu, 2) != 0;
#endif
		return true;
	} else {
		return false;
	}
}

/******************************************************************************
 * Lifecycle
 ******************************************************************************/

/* How to tell if a thread's life is beginning or ending */
bool kern_forking(unsigned int eip)
{
	return eip == TELL_LANDSLIDE_FORKING;
}
bool kern_sleeping(unsigned int eip)
{
	return eip == TELL_LANDSLIDE_SLEEPING;
}
bool kern_vanishing(unsigned int eip)
{
	return eip == TELL_LANDSLIDE_VANISHING;
}
bool kern_readline_enter(unsigned int eip)
{
#ifdef PINTOS_KERNEL
	return false;
#else
	return eip == GUEST_READLINE_WINDOW_ENTER;
#endif
}
bool kern_readline_exit(unsigned int eip)
{
#ifdef PINTOS_KERNEL
	return false;
#else
	return eip == GUEST_READLINE_WINDOW_EXIT;
#endif
}
bool kern_exec_enter(unsigned int eip)
{
	if (TESTING_USERSPACE == 1) {
#ifdef GUEST_EXEC_ENTER
		return eip == GUEST_EXEC_ENTER;
#else
		assert(false && "EXEC must be defined if testing userspace");
		return false;
#endif
	} else {
		assert(false && "kernel exec hook not needed for kernelspace");
		return false;
	}
}

/* How to tell if a new thread is appearing or disappearing on the runqueue. */
bool kern_thread_runnable(conf_object_t *cpu, unsigned int eip, unsigned int *tid)
{
	if (eip == TELL_LANDSLIDE_THREAD_RUNNABLE) {
		/* 0(%esp) points to the return address; get the arg above it */
		*tid = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

bool kern_thread_descheduling(conf_object_t *cpu, unsigned int eip, unsigned int *tid)
{
	if (eip == TELL_LANDSLIDE_THREAD_DESCHEDULING) {
		*tid = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
}

bool kern_beginning_vanish_before_unreg_process(unsigned int eip)
{
	// FIXME #130 - use a separate option than THREAD_KILLED here.
#ifdef GUEST_THREAD_KILLED
	return eip == GUEST_THREAD_KILLED;
#else
	return false;
#endif
}

/******************************************************************************
 * LMM
 ******************************************************************************/

bool kern_lmm_alloc_entering(conf_object_t *cpu, unsigned int eip, unsigned int *size)
{
	if (eip == GUEST_LMM_ALLOC_ENTER) {
		*size = READ_STACK(cpu, GUEST_LMM_ALLOC_SIZE_ARGNUM);
		return true;
#ifdef GUEST_LMM_ALLOC_GEN_ENTER
	} else if (eip == GUEST_LMM_ALLOC_GEN_ENTER) {
		*size = READ_STACK(cpu, GUEST_LMM_ALLOC_GEN_SIZE_ARGNUM);
		return true;
#endif
	} else {
		return false;
	}
}

bool kern_lmm_alloc_exiting(conf_object_t *cpu, unsigned int eip, unsigned int *base)
{
	if (eip == GUEST_LMM_ALLOC_EXIT
#ifdef GUEST_LMM_ALLOC_GEN_EXIT
	    || eip == GUEST_LMM_ALLOC_GEN_EXIT
#endif
	    ) {
		*base = GET_CPU_ATTR(cpu, eax);
		return true;
	} else {
		return false;
	}
}

bool kern_lmm_free_entering(conf_object_t *cpu, unsigned int eip, unsigned int *base)
{
	if (eip == GUEST_LMM_FREE_ENTER) {
		*base = READ_STACK(cpu, GUEST_LMM_FREE_BASE_ARGNUM);
		return true;
	} else {
		return false;
	}
}

bool kern_lmm_free_exiting(unsigned int eip)
{
	return (eip == GUEST_LMM_FREE_EXIT);
}

bool kern_lmm_init_entering(unsigned int eip)
{
	return eip == GUEST_LMM_INIT_ENTER;
}

bool kern_lmm_init_exiting(unsigned int eip)
{
	return eip == GUEST_LMM_INIT_EXIT;
}


#ifdef PINTOS_KERNEL

bool kern_page_alloc_entering(conf_object_t *cpu, unsigned int eip, unsigned int *size)
{
	if (eip == GUEST_PALLOC_ALLOC_ENTER) {
		*size = READ_STACK(cpu, GUEST_PALLOC_ALLOC_SIZE_ARGNUM)
			* GUEST_PALLOC_ALLOC_SIZE_FACTOR;
		return true;
	} else {
		return false;
	}
}

bool kern_page_alloc_exiting(conf_object_t *cpu, unsigned int eip, unsigned int *base)
{
	if (eip == GUEST_PALLOC_ALLOC_EXIT) {
		*base = GET_CPU_ATTR(cpu, eax);
		return true;
	} else {
		return false;
	}
}

bool kern_page_free_entering(conf_object_t *cpu, unsigned int eip, unsigned int *base)
{
	if (eip == GUEST_PALLOC_FREE_ENTER) {
		*base = READ_STACK(cpu, GUEST_PALLOC_FREE_BASE_ARGNUM);
		return true;
	} else {
		return false;
	}
}

bool kern_page_free_exiting(unsigned int eip)
{
	return eip == GUEST_PALLOC_FREE_EXIT;
}

#else

bool kern_page_alloc_entering(conf_object_t *cpu, unsigned int eip, unsigned int *size) { return false; }
bool kern_page_alloc_exiting(conf_object_t *cpu, unsigned int eip, unsigned int *base) { return false; }
bool kern_page_free_entering(conf_object_t *cpu, unsigned int eip, unsigned int *base) { return false; }
bool kern_page_free_exiting(unsigned int eip) { return false; }

#endif


static bool kern_address_in_vga_console(unsigned int addr)
{
#ifdef PINTOS_KERNEL
	return addr >= GUEST_VGA_CONSOLE_BASE && addr < GUEST_VGA_CONSOLE_END;
#else
	return false;
#endif
}

bool kern_address_in_heap(unsigned int addr)
{
	return (addr >= GUEST_IMG_END && KERNEL_MEMORY(addr)) &&
		!kern_address_in_vga_console(addr);
}

bool kern_address_global(unsigned int addr)
{
	return (addr >= GUEST_DATA_START && addr < GUEST_DATA_END) ||
		(addr >= GUEST_BSS_START && addr < GUEST_BSS_END) ||
		kern_address_in_vga_console(addr);
}

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
#ifdef GUEST_SHELL_TID
	return GUEST_SHELL_TID;
#else
	assert(false && "This kernel does not have a shell!");
	return -1;
#endif
}

bool kern_has_shell()
{
#ifdef GUEST_SHELL_TID
	return true;
#else
	return false;
#endif
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
                       void (*add_thread)(struct sched_state *, unsigned int tid,
                                          bool on_runqueue))
{
	GUEST_STARTING_THREAD_CODE;
}

bool kern_wants_us_to_dump_stack(unsigned int eip)
{
#ifdef TELL_LANDSLIDE_DUMP_STACK
	return eip == TELL_LANDSLIDE_DUMP_STACK;
#else
	return false;
#endif
}

bool kern_vm_user_copy_enter(unsigned int eip)
{
#ifdef GUEST_VM_USER_COPY_ENTER
	return eip == GUEST_VM_USER_COPY_ENTER;
#else
	return false;
#endif
}

bool kern_vm_user_copy_exit(unsigned int eip)
{
#ifdef GUEST_VM_USER_COPY_EXIT
#ifndef GUEST_VM_USER_COPY_ENTER
	STATIC_ASSERT(false);
#endif
	return eip == GUEST_VM_USER_COPY_EXIT;
#else
	return false;
#endif
}
