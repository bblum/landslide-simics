/**
 * @file user_specifics.c
 * @brief Guest-dependent-but-agnostic things landslide needs to know
 * @author Ben Blum
 */

#include <assert.h>
#include <simics/api.h>

#define MODULE_NAME "user glue"

#include "common.h"
#include "compiler.h"
#include "kernel_specifics.h"
#include "user_specifics.h"
#include "x86.h"

bool testing_userspace()
{
	return TESTING_USERSPACE == 1;
}

bool user_within_functions(conf_object_t *cpu, int eip)
{
	static const int within_functions[][3] = USER_WITHIN_FUNCTIONS;
	int length = ARRAY_SIZE(within_functions);
	return _within_functions(cpu, eip, within_functions, length);
}

bool user_yielding(int eip)
{
#ifdef USER_YIELD_ENTER
	return eip == USER_YIELD_ENTER;
#else
	return false;
#endif
}

/******************************************************************************
 * Malloc
 ******************************************************************************/

bool user_mm_init_entering(int eip)
{
#ifdef USER_MM_INIT_ENTER
	return eip == USER_MM_INIT_ENTER;
#else
	return false;
#endif
}

bool user_mm_init_exiting(int eip)
{
#ifdef USER_MM_INIT_EXIT
	return eip == USER_MM_INIT_EXIT;
#else
#ifdef USER_MM_INIT_ENTER
	STATIC_ASSERT(false && "user mm_init enter but not exit defined");
#endif
	return false;
#endif
}

bool user_mm_malloc_entering(conf_object_t *cpu, int eip, int *size)
{
#ifdef USER_MM_MALLOC_ENTER
	if (eip == USER_MM_MALLOC_ENTER) {
		*size = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}

bool user_mm_malloc_exiting(conf_object_t *cpu, int eip, int *base)
{
#ifdef USER_MM_MALLOC_EXIT
	if (eip == USER_MM_MALLOC_EXIT) {
		*base = GET_CPU_ATTR(cpu, eax);
		return true;
	} else {
		return false;
	}
#else
#ifdef USER_MM_MALLOC_ENTER
	STATIC_ASSERT(false && "user malloc enter but not exit defined");
#endif
	return false;
#endif
}

bool user_mm_free_entering(conf_object_t *cpu, int eip, int *base)
{
#ifdef USER_MM_FREE_ENTER
	if (eip == USER_MM_FREE_ENTER) {
		*base = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}

bool user_mm_free_exiting(int eip)
{
#ifdef USER_MM_FREE_EXIT
	return (eip == USER_MM_FREE_EXIT);
#else
#ifdef USER_MM_FREE_ENTER
	STATIC_ASSERT(false && "user free enter but not exit defined");
#endif
	return false;
#endif
}

/******************************************************************************
 * ELF regions
 ******************************************************************************/

bool user_address_in_heap(int addr)
{
#ifdef USER_IMG_END
	/* Note: We also want to exclude stack addresses, but those are
	 * typically 0xfXXXXXXX, and the signed comparison will "conveniently"
	 * exclude those. Gross, but whatever. */
	return (addr >= USER_IMG_END);
#else
	return false;
#endif
}

bool user_address_global(int addr)
{
#if defined(USER_DATA_START) && defined(USER_DATA_END) && defined(USER_BSS_START) &&  defined(USER_IMG_END)
	return ((addr >= USER_DATA_START && addr < USER_DATA_END) ||
		(addr >= USER_BSS_START  && addr < USER_IMG_END));
#else
	return false;
#endif
}

bool user_panicked(conf_object_t *cpu, int addr, char **buf)
{
#ifdef USER_PANIC_ENTER
	if (addr == USER_PANIC_ENTER) {
		read_panic_message(cpu, addr, buf);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}

/******************************************************************************
 * Thread library
 ******************************************************************************/

bool user_thr_init_entering(int eip) {
#ifdef USER_THR_INIT_ENTER
	return eip == USER_THR_INIT_ENTER;
#else
	return false;
#endif
}
bool user_thr_init_exiting(int eip) {
#ifdef USER_THR_INIT_EXIT
	return eip == USER_THR_INIT_EXIT;
#else
#ifdef USER_THR_INIT_ENTER
	STATIC_ASSERT(false && "THR_INIT ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_thr_create_entering(int eip) {
#ifdef USER_THR_CREATE_ENTER
	return eip == USER_THR_CREATE_ENTER;
#else
	return false;
#endif
}
bool user_thr_create_exiting(int eip) {
#ifdef USER_THR_CREATE_EXIT
	return eip == USER_THR_CREATE_EXIT;
#else
#ifdef USER_THR_CREATE_ENTER
	STATIC_ASSERT(false && "THR_CREATE ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_thr_join_entering(int eip) {
#ifdef USER_THR_JOIN_ENTER
	return eip == USER_THR_JOIN_ENTER;
#else
	return false;
#endif
}
bool user_thr_join_exiting(int eip) {
#ifdef USER_THR_JOIN_EXIT
	return eip == USER_THR_JOIN_EXIT;
#else
#ifdef USER_THR_JOIN_ENTER
	STATIC_ASSERT(false && "THR_JOIN ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_thr_exit_entering(int eip) {
#ifdef USER_THR_EXIT_ENTER
	return eip == USER_THR_EXIT_ENTER;
#else
	return false;
#endif
}
bool user_thr_exit_exiting(int eip) {
#ifdef USER_THR_EXIT_EXIT
	return eip == USER_THR_EXIT_EXIT;
#else
#ifdef USER_THR_EXIT_ENTER
	STATIC_ASSERT(false && "THR_EXIT ENTER but not EXIT defined");
#endif
	return false;
#endif
}

/******************************************************************************
 * Mutexes
 ******************************************************************************/

bool user_mutex_lock_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_MUTEX_LOCK_ENTER
	if (eip == USER_MUTEX_LOCK_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_mutex_lock_exiting(int eip) {
#ifdef USER_MUTEX_LOCK_EXIT
	return eip == USER_MUTEX_LOCK_EXIT;
#else
#ifdef USER_MUTEX_LOCK_ENTER
	STATIC_ASSERT(false && "MUTEX_LOCK ENTER but not EXIT defined");
#endif
	return false;
#endif
}
/* The following two functions look funny because of two possible names. */
bool user_mutex_trylock_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_MUTEX_TRYLOCK_ENTER
	if (eip == USER_MUTEX_TRYLOCK_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
#ifdef USER_MUTEX_TRY_LOCK_ENTER
	if (eip == USER_MUTEX_TRY_LOCK_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
#endif
}
// XXX XXX XXX: Other student trylock implementations may return false/true
// instead of -1/0 like POBBLES does. How can we deal with this??
#define TRYLOCK_SUCCESS_VAL 0
bool user_mutex_trylock_exiting(conf_object_t *cpu, int eip, bool *succeeded) {
#ifdef USER_MUTEX_TRYLOCK_EXIT
	if (eip == USER_MUTEX_TRYLOCK_EXIT) {
		*succeeded = GET_CPU_ATTR(cpu, eax) == TRYLOCK_SUCCESS_VAL;
		return true;
	} else {
		return false;
	}
#else
#ifdef USER_MUTEX_TRYLOCK_ENTER
	STATIC_ASSERT(false && "MUTEX_TRYLOCK ENTER but not EXIT defined");
#endif
#ifdef USER_MUTEX_TRY_LOCK_EXIT
	if (eip == USER_MUTEX_TRYLOCK_EXIT) {
		*succeeded = GET_CPU_ATTR(cpu, eax) == TRYLOCK_SUCCESS_VAL;
		return true;
	} else {
		return false;
	}
#else
#ifdef USER_MUTEX_TRY_LOCK_ENTER
	STATIC_ASSERT(false && "MUTEX_TRY_LOCK ENTER but not EXIT defined");
#endif
	return false;
#endif
#endif
}
bool user_mutex_unlock_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_MUTEX_UNLOCK_ENTER
	if (eip == USER_MUTEX_UNLOCK_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_mutex_unlock_exiting(int eip) {
#ifdef USER_MUTEX_UNLOCK_EXIT
	return eip == USER_MUTEX_UNLOCK_EXIT;
#else
#ifdef USER_MUTEX_UNLOCK_ENTER
	STATIC_ASSERT(false && "MUTEX_UNLOCK ENTER but not EXIT defined");
#endif
	return false;
#endif
}

/******************************************************************************
 * Cvars
 ******************************************************************************/

bool user_cond_wait_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_COND_WAIT_ENTER
	if (eip == USER_COND_WAIT_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_cond_wait_exiting(int eip) {
#ifdef USER_COND_WAIT_EXIT
	return eip == USER_COND_WAIT_EXIT;
#else
#ifdef USER_COND_WAIT_ENTER
	STATIC_ASSERT(false && "COND_WAIT ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_cond_signal_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_COND_SIGNAL_ENTER
	if (eip == USER_COND_SIGNAL_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_cond_signal_exiting(int eip) {
#ifdef USER_COND_SIGNAL_EXIT
	return eip == USER_COND_SIGNAL_EXIT;
#else
#ifdef USER_COND_SIGNAL_ENTER
	STATIC_ASSERT(false && "COND_SIGNAL ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_cond_broadcast_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_COND_BROADCAST_ENTER
	if (eip == USER_COND_BROADCAST_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_cond_broadcast_exiting(int eip) {
#ifdef USER_COND_BROADCAST_EXIT
	return eip == USER_COND_BROADCAST_EXIT;
#else
#ifdef USER_COND_BROADCAST_ENTER
	STATIC_ASSERT(false && "COND_BROADCAST ENTER but not EXIT defined");
#endif
	return false;
#endif
}

/******************************************************************************
 * Semaphores
 ******************************************************************************/

bool user_sem_wait_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_SEM_WAIT_ENTER
	if (eip == USER_SEM_WAIT_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_sem_wait_exiting(int eip) {
#ifdef USER_SEM_WAIT_EXIT
	return eip == USER_SEM_WAIT_EXIT;
#else
#ifdef USER_SEM_WAIT_ENTER
	STATIC_ASSERT(false && "SEM_WAIT ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_sem_signal_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_SEM_SIGNAL_ENTER
	if (eip == USER_SEM_SIGNAL_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_sem_signal_exiting(int eip) {
#ifdef USER_SEM_SIGNAL_EXIT
	return eip == USER_SEM_SIGNAL_EXIT;
#else
#ifdef USER_SEM_SIGNAL_ENTER
	STATIC_ASSERT(false && "SEM_SIGNAL ENTER but not EXIT defined");
#endif
	return false;
#endif
}

/******************************************************************************
 * RWlocks
 ******************************************************************************/

/* Mode: 0 == read; 1 == write */
bool user_rwlock_lock_entering(conf_object_t *cpu, int eip, int *addr, bool *write) {
#ifdef USER_RWLOCK_LOCK_ENTER
	if (eip == USER_RWLOCK_LOCK_ENTER) {
		*addr = READ_STACK(cpu, 1);
		*write = READ_STACK(cpu, 2) != 0;
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_rwlock_lock_exiting(int eip) {
#ifdef USER_RWLOCK_LOCK_EXIT
	return eip == USER_RWLOCK_LOCK_EXIT;
#else
#ifdef USER_RWLOCK_LOCK_ENTER
	STATIC_ASSERT(false && "RWLOCK_LOCK ENTER but not EXIT defined");
#endif
	return false;
#endif
}
bool user_rwlock_unlock_entering(conf_object_t *cpu, int eip, int *addr) {
#ifdef USER_RWLOCK_UNLOCK_ENTER
	if (eip == USER_RWLOCK_UNLOCK_ENTER) {
		*addr = READ_STACK(cpu, 1);
		return true;
	} else {
		return false;
	}
#else
	return false;
#endif
}
bool user_rwlock_unlock_exiting(int eip) {
#ifdef USER_RWLOCK_UNLOCK_EXIT
	return eip == USER_RWLOCK_UNLOCK_EXIT;
#else
#ifdef USER_RWLOCK_UNLOCK_ENTER
	STATIC_ASSERT(false && "RWLOCK_UNLOCK ENTER but not EXIT defined");
#endif
	return false;
#endif
}
