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

void lockset_init(struct lockset *l)
{
	l->num_locks = 0;
	l->capacity = MAX_LOCKS;
	l->locks = MM_XMALLOC(MAX_LOCKS, struct lock);
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

void lockset_add(struct lockset *l, int lock_addr, enum lock_type type)
{
	assert(l->num_locks < l->capacity - 1 &&
	       "Max number of locks in lockset implementation exceeded :(");

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

	l->locks[l->num_locks].addr  = lock_addr;
	l->locks[l->num_locks].type = type;
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

bool lockset_intersect(struct lockset *l1, struct lockset *l2)
{
	// Bad runtime. Oh well.
	for (int i = 0; i < l1->num_locks; i++) {
		for (int j = 0; j < l2->num_locks; j++) {
			if (l1->locks[i].addr == l2->locks[j].addr &&
			    /* at least one lock is held in write mode */
			    SAME_LOCK_TYPE(l1->locks[i].type, l2->locks[j].type)) {
				return true;
			}
		}
	}
	return false;
}

enum lockset_cmp_result lockset_compare(struct lockset *l1, struct lockset *l2)
{
	// TODO: Implement better later.
	if (l1->num_locks == 0 && l2->num_locks == 0) {
		return LOCKSETS_EQ;
	} else if (l1->num_locks == 0) {
		return LOCKSETS_SUBSET;
	} else if (l2->num_locks == 0) {
		return LOCKSETS_SUPSET;
	} else if (l1->num_locks == 1 && l2->num_locks == 1 &&
		   l1->locks[0].addr == l2->locks[0].addr &&
		   l1->locks[0].type == l2->locks[0].type) {
		return LOCKSETS_EQ;
	} else {
		return LOCKSETS_DIFF;
	}
}
