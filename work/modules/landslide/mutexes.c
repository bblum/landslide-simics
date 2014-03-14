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
	Q_INIT_HEAD(&m->user_mutexes);
}

/* register a malloced chunk as belonging to a particular mutex.
 * will add mutex to the list of all mutexes if it's not already there. */
void learn_malloced_mutex_structure(struct mutex_state *m, int lock_addr,
				    int chunk_addr, int chunk_size)
{
	struct mutex *mp;
	assert(lock_addr != -1);
	Q_SEARCH(mp, &m->user_mutexes, nobe, mp->addr == (unsigned int)lock_addr);
	if (mp == NULL) {
		lsprintf(DEV, "created user mutex 0x%x (%d others)\n",
			 lock_addr, (int)Q_GET_SIZE(&m->user_mutexes));
		mp = MM_XMALLOC(1, struct mutex);
		mp->addr = (unsigned int)lock_addr;
		Q_INIT_HEAD(&mp->chunks);
		Q_INSERT_FRONT(&m->user_mutexes, mp, nobe);
	}

	struct mutex_chunk *c;
	Q_SEARCH(c, &mp->chunks, nobe, c->base == chunk_addr);
	assert(c == NULL && "chunk already exists");
	c = MM_XMALLOC(1, struct mutex_chunk);
	c->base = (unsigned int)chunk_addr;
	c->size = (unsigned int)chunk_size;
	Q_INSERT_FRONT(&mp->chunks, c, nobe);
	lsprintf(DEV, "user mutex 0x%x grows chunk [0x%x | %d]\n",
		 lock_addr, chunk_addr, chunk_size);
}

/* forget about a mutex that no longer exists. */
void mutex_destroy(struct mutex_state *m, int lock_addr)
{
	struct mutex *mp;
	Q_SEARCH(mp, &m->user_mutexes, nobe, mp->addr == (unsigned int)lock_addr);
	if (mp != NULL) {
		lsprintf(DEV, "forgetting about user mutex 0x%x (chunks:", lock_addr);
		Q_REMOVE(&m->user_mutexes, mp, nobe);
		while (Q_GET_SIZE(&mp->chunks) > 0) {
			struct mutex_chunk *c = Q_GET_HEAD(&mp->chunks);
			printf(DEV, " [0x%x | %d]", c->base, c->size);
			Q_REMOVE(&mp->chunks, c, nobe);
			MM_FREE(c);
		}
		printf(DEV, ")\n");
		MM_FREE(mp);

		Q_SEARCH(mp, &m->user_mutexes, nobe, mp->addr == lock_addr);
		assert(mp == NULL && "user mutex existed twice??");
	}
}

static bool lock_contains_addr(struct mutex_state *m,
			       unsigned int lock_addr, unsigned int addr)
{
	if (addr >= lock_addr && addr < lock_addr + m->size) {
		return true;
	} else {
		/* search heap chunks of known malloced mutexes */
		struct mutex *mp;
		Q_SEARCH(mp, &m->user_mutexes, nobe, mp->addr == lock_addr);
		if (mp == NULL) {
			return false;
		} else {
			struct mutex_chunk *c;
			Q_SEARCH(c, &mp->chunks, nobe,
				 addr >= c->base && addr < c->base + c->size);
			return c != NULL;
		}
	}
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
		    lock_contains_addr(m, (unsigned int)lock_addr, addr)) {
			lsprintf(DEV, "Rogue write to %x, unblocks tid %d from %x\n",
				 addr, a->tid, lock_addr);
			a->user_blocked_on_addr = -1;
		}
	);
}
