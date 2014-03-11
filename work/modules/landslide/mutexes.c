/**
 * @file mutexes.c
 * @brief state for modeling userspace mutex behaviour
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#define MODULE_NAME "MUTEX"
#define MODULE_COLOUR COLOUR_DARK COLOUR_GREY

#include "common.h"
#include "user_specifics.h"
#include "landslide.h"
#include "mutexes.h"
#include "variable_queue.h"

void mutexes_init(struct mutex_state *m)
{
	m->size = 0;
}

#define MUTEX_TYPE_NAME "mutex_t"
#define DEFAULT_USER_MUTEX_SIZE 8 /* what to use if we can't find one */

/* Some p2s may have open-coded mutex unlock actions (outside of mutex_*()
 * functions) to help with thread exiting. Detect these and unblock contenders. */
void check_user_mutex_access(struct ls_state *ls, unsigned int addr)
{
	struct mutex_state *m = &ls->mutexes;
	assert(addr >= USER_MEM_START);
	assert(ls->user_mem.cr3 != 0 && "can't check user mutex before cr3 is known");

	if (m->size == 0) {
		// Learning user mutex size relies on the user symtable being
		// registered. Delay this until one surely has been.
		int _lock_addr;
		if (user_mutex_init_entering(ls->cpu0, ls->eip, &_lock_addr)) {
			int size;
			if (find_user_global_of_type(MUTEX_TYPE_NAME, &size)) {
				m->size = size;
			} else {
				lsprintf(DEV, COLOUR_BOLD COLOUR_YELLOW
					 "WARNING: Assuming %d-byte mutexes.\n",
					 DEFAULT_USER_MUTEX_SIZE);
				m->size = DEFAULT_USER_MUTEX_SIZE;
			}
		} else {
			return;
		}
	}
	assert(m->size > 0);

	struct agent *a = ls->sched.cur_agent;
	if (a->action.user_mutex_locking || a->action.user_mutex_unlocking) {
		return;
	}

	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		unsigned int lock_addr = (unsigned int)a->user_blocked_on_addr;
		if (lock_addr != (unsigned int)(-1) &&
		    addr >= lock_addr && addr < lock_addr + m->size) {
			lsprintf(DEV, "Rogue write to %x, unblocks tid %d from %x\n",
				 addr, a->tid, lock_addr);
			a->user_blocked_on_addr = -1;
		}
	);
}
