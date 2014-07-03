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
	l->num_locks = 0;
	l->capacity = 16;
	l->locks = MM_XMALLOC(l->capacity, struct lock);
}

void lockset_free(struct lockset *l)
{
	MM_FREE(l->locks);
}

void lockset_clone(struct lockset *dest, struct lockset *src)
{
	dest->num_locks = src->num_locks;
	dest->capacity  = src->num_locks; // space optimization for long-term storage
	dest->locks = MM_XMALLOC(src->num_locks, struct lock);
	memcpy(dest->locks, src->locks, src->num_locks * sizeof(struct lock));
}

void lockset_print(verbosity v, struct lockset *l)
{
	for (int i = 0; i < l->num_locks; i++) {
		if (i != 0) {
			printf(v, ", ");
		}
		printf(v, "0x%x%s", l->locks[i].addr,
		       l->locks[i].type == LOCK_MUTEX ? "" :
		       l->locks[i].type == LOCK_SEM ? "(s)" :
		       l->locks[i].type == LOCK_RWLOCK ? "(w)" :
		       l->locks[i].type == LOCK_RWLOCK_READ ? "(r)" :
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
	/* expand array if necessary */
	assert(l->num_locks <= l->capacity);
	if (l->num_locks == l->capacity) {
		struct lock *old_array = l->locks;
		assert(l->capacity < UINT_MAX / 2);
		l->capacity *= 2;
		l->locks = MM_XMALLOC(l->capacity, typeof(*l->locks));
		memcpy(l->locks, old_array, l->num_locks * sizeof(*l->locks));
		MM_FREE(old_array);
	}

	lsprintf(INFO, "Adding 0x%x to lockset: ", lock_addr);
	lockset_print(INFO, l);
	printf(INFO, "\n");

	/* Check that the lock is not already held. Make an exception for
	 * e.g. mutexes and things that can contain them having the same
	 * address. */
	for (int i = 0; i < l->num_locks; i++) {
		if (SAME_LOCK_TYPE(l->locks[i].type, type)) {
			assert(l->locks[i].addr != lock_addr &&
			       "Recursive locking not supported");
		}
	}

	l->locks[l->num_locks].addr = lock_addr;
	l->locks[l->num_locks].type = type;

	/* sort */
	for (unsigned int i = l->num_locks; i > 0; i--) {
		int cmp = lock_cmp(&l->locks[i].addr, &l->locks[i - 1]);
		assert(cmp != 0); /* invariant maintained by check above */
		if (cmp < 0) {
			/* swap */
			enum lock_type tmp_type = l->locks[i - 1].type;
			int tmp_addr            = l->locks[i - 1].addr;
			l->locks[i - 1].type = l->locks[i].type;
			l->locks[i - 1].addr = l->locks[i].addr;
			l->locks[i].type = tmp_type;
			l->locks[i].addr = tmp_addr;
		} else {
			break;
		}
	}

	l->num_locks++;
}

static bool _lockset_remove(struct lockset *l, int lock_addr, enum lock_type type)
{
	assert(l->num_locks < l->capacity);

	assert(type != LOCK_RWLOCK_READ && "use LOCK_RWLOCK for unlocking");

	lsprintf(INFO, "Removing 0x%x from lockset: ", lock_addr);
	lockset_print(INFO, l);
	printf(INFO, "\n");

	for (int i = 0; i < l->num_locks; i++) {
		if (l->locks[i].addr == lock_addr &&
		    SAME_LOCK_TYPE(l->locks[i].type, type)) {
			l->num_locks--;
			/* swap last lock into this lock's position */
			l->locks[i].addr = l->locks[l->num_locks].addr;
			l->locks[i].type = l->locks[l->num_locks].type;
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
	for (int i = 0; i < l0->num_locks; i++) {
		for (int j = 0; j < l1->num_locks; j++) {
			if (l0->locks[i].addr == l1->locks[j].addr &&
			    /* at least one lock is held in write mode */
			    SAME_LOCK_TYPE(l0->locks[i].type, l1->locks[j].type)) {
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

	while (i < l0->num_locks || j < l1->num_locks) {
		/* check termination condition */
		if (i == l0->num_locks) {
			/* j's set has extra elements */
			if (result == LOCKSETS_SUPSET) {
				return LOCKSETS_DIFF;
			} else {
				return LOCKSETS_SUBSET;
			}
		} else if (j == l1->num_locks) {
			/* i's set has extra elements */
			if (result == LOCKSETS_SUBSET) {
				return LOCKSETS_DIFF;
			} else {
				return LOCKSETS_SUPSET;
			}
		}

		/* check elements */
		int cmp = lock_cmp(&l0->locks[i], &l1->locks[j]);
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
