/**
 * @file lockset.c
 * @brief Data structures for data race detection
 * @author Ben Blum
 */

#define MODULE_NAME "LOCKSET"
#define MODULE_COLOUR COLOUR_DARK COLOUR_BLUE

#include "common.h"
#include "landslide.h"
#include "lockset.h"
#include "schedule.h"
#include "stack.h"
#include "symtable.h"

void lockset_init(struct lockset *l)
{
	ARRAY_LIST_INIT(&l->list, 16);
}

void lockset_free(struct lockset *l)
{
	ARRAY_LIST_FREE(&l->list);
}

void lockset_clone(struct lockset *dest, struct lockset *src)
{
	ARRAY_LIST_CLONE(&dest->list, &src->list);
}

void lockset_print(verbosity v, struct lockset *l)
{
	unsigned int i;
	struct lock *lock;
	ARRAY_LIST_FOREACH(&l->list, i, lock) {
		if (i != 0) {
			printf(v, ", ");
		}
		printf(v, "0x%x%s", lock->addr,
		       lock->type == LOCK_MUTEX ? "" :
		       lock->type == LOCK_SEM ? "(s)" :
		       lock->type == LOCK_RWLOCK ? "(w)" :
		       lock->type == LOCK_RWLOCK_READ ? "(r)" :
		       "(unknown)");
	}
}

static int lock_cmp(struct lock *lock0, struct lock *lock1)
{
	if (lock0->addr < lock1->addr) {
		return -1;
	} else if (lock0->addr > lock1->addr) {
		return 1;
	} else if (lock0->type < lock1->type) {
		return -2;
	} else if (lock0->type > lock1->type) {
		return 2;
	} else {
		return 0;
	}
}

void lockset_add(struct lockset *l, int lock_addr, enum lock_type type)
{
	lsprintf(INFO, "Adding 0x%x to lockset: ", lock_addr);
	lockset_print(INFO, l);
	printf(INFO, "\n");

	/* Check that the lock is not already held. Make an exception for
	 * e.g. mutexes and things that can contain them having the same
	 * address. */
	unsigned int i;
	struct lock *lock;
	ARRAY_LIST_FOREACH(&l->list, i, lock) {
		if (SAME_LOCK_TYPE(lock->type, type)) {
			assert(lock->addr != lock_addr &&
			       "Recursive locking not supported");
		}
	}

	struct lock new_lock = { .addr = lock_addr, .type = type };
	ARRAY_LIST_APPEND(&l->list, new_lock);

	/* sort */
	assert(ARRAY_LIST_SIZE(&l->list) > 0);
	for (unsigned int i = ARRAY_LIST_SIZE(&l->list) - 1; i > 0; i--) {
		assert(i >= 1);
		struct lock *this_lock  = ARRAY_LIST_GET(&l->list, i);
		struct lock *other_lock = ARRAY_LIST_GET(&l->list, i - 1);
		int cmp = lock_cmp(this_lock, other_lock);
		assert(cmp != 0); /* invariant maintained by check above */
		if (cmp < 0) {
			ARRAY_LIST_SWAP(&l->list, i, i - 1);
		} else {
			/* lock is now in sorted position */
			break;
		}
	}
}

static bool _lockset_remove(struct lockset *l, int lock_addr, enum lock_type type)
{
	assert(type != LOCK_RWLOCK_READ && "use LOCK_RWLOCK for unlocking");

	lsprintf(INFO, "Removing 0x%x from lockset: ", lock_addr);
	lockset_print(INFO, l);
	printf(INFO, "\n");

	int i;
	struct lock *lock;
	ARRAY_LIST_FOREACH(&l->list, i, lock) {
		if (lock->addr == lock_addr && SAME_LOCK_TYPE(lock->type, type)) {
			ARRAY_LIST_REMOVE(&l->list, i);
			return true;
		}
	}
	return false;
}

#define LOCKSET_OF(a, in_kernel) \
	((in_kernel) ? &(a)->kern_locks_held : &(a)->user_locks_held)
void lockset_remove(struct sched_state *s, int lock_addr, enum lock_type type,
		    bool in_kernel)
{
	if (_lockset_remove(LOCKSET_OF(s->cur_agent, in_kernel), lock_addr, type))
		return;

	char lock_name[32];
	symtable_lookup_data(lock_name, 32, lock_addr);
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_YELLOW "WARNING: Lock handoff with "
		 "TID %d unlocking %s @ 0x%x; expect data race tracking may be "
		 "incorrect\n" COLOUR_DEFAULT, s->cur_agent->tid, lock_name, lock_addr);

#ifdef ALLOW_LOCK_HANDOFF
	struct agent *a;
	Q_FOREACH(a, &s->rq, nobe) {
		if (_lockset_remove(LOCKSET_OF(a, in_kernel), lock_addr, type)) return;
	}
	Q_FOREACH(a, &s->sq, nobe) {
		if (_lockset_remove(LOCKSET_OF(a, in_kernel), lock_addr, type)) return;
	}
	Q_FOREACH(a, &s->rq, nobe) {
		if (_lockset_remove(LOCKSET_OF(a, in_kernel), lock_addr, type)) return;
	}
#endif

	// Lock not found.
	lsprintf(ALWAYS, COLOUR_BOLD COLOUR_YELLOW "WARNING: Couldn't find "
		 "unlock()ed lock 0x%x in lockset; probably incorrect "
		 "annotations - did you forget to annotate mutex_trylock()?\n"
		 COLOUR_DEFAULT, lock_addr);
	// In userspace this is a warning instead of a panic. Bad "annotations"
	// are our fault, and some p2s may do ridiculous stuff like POBBLES's
	// "copy_info in thr_join" thing. Just ignore it and move along.
	if (in_kernel) {
		LS_ABORT();
	}
}
#undef LOCKSET_OF

bool lockset_intersect(struct lockset *l0, struct lockset *l1)
{
	// Bad runtime. Oh well.
	// TODO: can exploit the fact that both are sorted for O(n+m) time
	struct lock *lock0;
	struct lock *lock1;
	int i, j;
	ARRAY_LIST_FOREACH(&l0->list, i, lock0) {
		ARRAY_LIST_FOREACH(&l1->list, j, lock1) {
			if (lock0->addr == lock1->addr &&
			    /* at least one lock is held in write mode */
			    SAME_LOCK_TYPE(lock0->type, lock1->type)) {
				return true;
			}
		}
	}
	return false;
}

enum lockset_cmp_result lockset_compare(struct lockset *l0, struct lockset *l1)
{
	enum lockset_cmp_result result = LOCKSETS_EQ;
	int i = 0, j = 0;

	while (i < ARRAY_LIST_SIZE(&l0->list) || j < ARRAY_LIST_SIZE(&l1->list)) {
		/* check termination condition */
		if (i == ARRAY_LIST_SIZE(&l0->list)) {
			/* j's set has extra elements */
			if (result == LOCKSETS_SUPSET) {
				return LOCKSETS_DIFF;
			} else {
				return LOCKSETS_SUBSET;
			}
		} else if (j == ARRAY_LIST_SIZE(&l1->list)) {
			/* i's set has extra elements */
			if (result == LOCKSETS_SUBSET) {
				return LOCKSETS_DIFF;
			} else {
				return LOCKSETS_SUPSET;
			}
		}

		/* check elements */
		int cmp = lock_cmp(ARRAY_LIST_GET(&l0->list, i),
				   ARRAY_LIST_GET(&l1->list, j));
		if (cmp < 0) {
			/* this lock is missing in j's set */
			if (result == LOCKSETS_SUBSET) {
				return LOCKSETS_DIFF;
			} else {
				result = LOCKSETS_SUPSET;
				i++;
			}
		} else if (cmp > 0) {
			/* this lock is missing in i's set */
			if (result == LOCKSETS_SUPSET) {
				return LOCKSETS_DIFF;
			} else {
				result = LOCKSETS_SUBSET;
				j++;
			}
		} else {
			i++;
			j++;
		}
	}

	return result;
}
