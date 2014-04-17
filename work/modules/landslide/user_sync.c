/**
 * @file user_sync.c
 * @brief state for modeling userspace synchronization behaviour
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#define MODULE_NAME "USER-SYNC"
#define MODULE_COLOUR COLOUR_DARK COLOUR_GREY

#include "common.h"
#include "landslide.h"
#include "schedule.h"
#include "tree.h"
#include "user_specifics.h"
#include "user_sync.h"
#include "variable_queue.h"
#include "x86.h"

void user_sync_init(struct user_sync_state *u)
{
	u->mutex_size = 0;
	Q_INIT_HEAD(&u->mutexes);
	u->yield_progress = NOTHING_INTERESTING;
	u->xchg_count = 0;
}

void user_yield_state_init(struct user_yield_state *y)
{
	y->loop_count = 0;
	y->blocked = false;
}

/******************************************************************************
 * Mutexes
 ******************************************************************************/

/* register a malloced chunk as belonging to a particular mutex.
 * will add mutex to the list of all mutexes if it's not already there. */
void learn_malloced_mutex_structure(struct user_sync_state *u, int lock_addr,
				    int chunk_addr, int chunk_size)
{
	struct mutex *mp;
	assert(lock_addr != -1);
	Q_SEARCH(mp, &u->mutexes, nobe, mp->addr == (unsigned int)lock_addr);
	if (mp == NULL) {
		lsprintf(DEV, "created user mutex 0x%x (%d others)\n",
			 lock_addr, (int)Q_GET_SIZE(&u->mutexes));
		mp = MM_XMALLOC(1, struct mutex);
		mp->addr = (unsigned int)lock_addr;
		Q_INIT_HEAD(&mp->chunks);
		Q_INSERT_FRONT(&u->mutexes, mp, nobe);
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
void mutex_destroy(struct user_sync_state *u, int lock_addr)
{
	struct mutex *mp;
	Q_SEARCH(mp, &u->mutexes, nobe, mp->addr == (unsigned int)lock_addr);
	if (mp != NULL) {
		lsprintf(DEV, "forgetting about user mutex 0x%x (chunks:", lock_addr);
		Q_REMOVE(&u->mutexes, mp, nobe);
		while (Q_GET_SIZE(&mp->chunks) > 0) {
			struct mutex_chunk *c = Q_GET_HEAD(&mp->chunks);
			printf(DEV, " [0x%x | %d]", c->base, c->size);
			Q_REMOVE(&mp->chunks, c, nobe);
			MM_FREE(c);
		}
		printf(DEV, ")\n");
		MM_FREE(mp);

		Q_SEARCH(mp, &u->mutexes, nobe, mp->addr == lock_addr);
		assert(mp == NULL && "user mutex existed twice??");
	}
}

static bool lock_contains_addr(struct user_sync_state *u,
			       unsigned int lock_addr, unsigned int addr)
{
	if (addr >= lock_addr && addr < lock_addr + u->mutex_size) {
		return true;
	} else {
		/* search heap chunks of known malloced mutexes */
		struct mutex *mp;
		Q_SEARCH(mp, &u->mutexes, nobe, mp->addr == lock_addr);
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
	struct user_sync_state *u = &ls->user_sync;
	assert(addr >= USER_MEM_START);
	assert(ls->user_mem.cr3 != 0 && "can't check user mutex before cr3 is known");

	if (u->mutex_size == 0) {
		// Learning user mutex size relies on the user symtable being
		// registered. Delay this until one surely has been.
		int _lock_addr;
		if (user_mutex_init_entering(ls->cpu0, ls->eip, &_lock_addr)) {
			int size;
			if (find_user_global_of_type(MUTEX_TYPE_NAME, &size)) {
				u->mutex_size = size;
			} else {
				lsprintf(DEV, COLOUR_BOLD COLOUR_YELLOW
					 "WARNING: Assuming %d-byte mutexes.\n",
					 DEFAULT_USER_MUTEX_SIZE);
				u->mutex_size = DEFAULT_USER_MUTEX_SIZE;
			}
		} else {
			return;
		}
	}
	assert(u->mutex_size > 0);

	struct agent *a = ls->sched.cur_agent;
	if (a->action.user_mutex_locking || a->action.user_mutex_unlocking) {
		return;
	}

	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		unsigned int lock_addr = (unsigned int)a->user_blocked_on_addr;
		if (lock_addr != (unsigned int)(-1) &&
		    lock_contains_addr(u, (unsigned int)lock_addr, addr)) {
			lsprintf(DEV, "Rogue write to %x, unblocks tid %d from %x\n",
				 addr, a->tid, lock_addr);
			a->user_blocked_on_addr = -1;
		}
	);
}

/******************************************************************************
 * Yield loops
 ******************************************************************************/

/* The approach to deriving if a user thread is "blocked" in an
 * open-coded yield loop involves waiting for it to do a certain number
 * of yields in a row. However, this pollutes the tree with extra
 * branches that are not actually progress, but just that thread ticking
 * up its yield-loop counter. If we treated these as real transitions,
 * we could end up tagging a sibling of every single one of them,
 * while really (assuming we are right that the yield loop is doing
 * nothing) it's a safe reduction technique to tag siblings of only one. */

/******************** DPOR-related ********************/

/* Scans the history of the branch ending in 'h0' and sets the yield-blocked
 * flag for branches "preceding" one where we realized a thread was blocked. */
void update_user_yield_blocked_transitions(struct hax *h0)
{
	/* Here we scan backwards looking for transitions where we realized
	 * a user thread was yield-blocked (its yield-loop counter hit max),
	 * and adjust the values of that thread's past transitions (where it
	 * was still counting up to the max) to reflect that those transitions
	 * are actually blocked as well.
	 *
	 * One important detail is that we leave the 1st yield-blocked
	 * transition marked unblocked (i.e., don't propagate the max counter
	 * value to it). This allows us to reorder other thread transitions
	 * to occur directly before it.
	 *
	 * While we're at it, of course, we also check the invariant that the
	 * yield-loop counters are either zero or counting up to the max. */

	for (struct hax *h = h0; h->parent != NULL; h = h->parent) {
		struct agent *a = find_runnable_agent(h->oldsched, h->chosen_thread);
		if (a == NULL) {
			/* The chosen thread vanished during its transition.
			 * Definitely not a yield-loop-blocked one. */
			continue;
		}

		assert(a->user_yield.loop_count >= 0);
		if (a->user_yield.loop_count < TOO_MANY_YIELDS) {
			/* Only perform the below (expensive) check when a
			 * thread actually became yield-loop-blocked. */
			continue;
		} else if (a->user_yield.blocked) {
			/* skip it if we already marked it as propagated. */
			lsprintf(DEV, "not propagating YB for #%d/tid%d "
				 "(already did)\n", h->depth, h->chosen_thread);
			continue;
		}
		int expected_count = a->user_yield.loop_count;

		lsprintf(DEV, "scanning ylc for #%d/tid%d, ylc after was %d\n",
			 h->depth, h->chosen_thread, a->user_yield.loop_count);

		/* Propagate backwards in time the fact that this sequence of
		 * yields reached the max number and is considered blocked.
		 * Start at h, not h->parent, to include the one we just saw. */
		for (struct hax *h2 = h; expected_count > 0; h2 = h2->parent) {
			assert(h2 != NULL && "nonzero yield count at root");

			/* Find the past version of the yield-blocked thread. */
			struct agent *a2 = find_runnable_agent(h2->oldsched, a->tid);
			assert(a2 != NULL && "yielding thread vanished in the past");

			/* Its count must have incremented between the end of
			 * this (old) transition and the more recent one. */
			assert(a2->user_yield.loop_count == expected_count);

			/* Skip marking the "first" yield-blocked transition. */
			if (expected_count > 1) {
				lsprintf(DEV,  "#%d/tid%d is YLB (count %d), "
					 "from #%d/tid%d\n",
					 h2->depth, h2->chosen_thread,
					 a2->user_yield.loop_count,
					 h->depth, h->chosen_thread);
				a2->user_yield.blocked = true;
			}

			/* Also, if that thread actually ran during this (old)
			 * transition, expect its count to have increased. */
			struct agent *a3 =
				find_runnable_agent(h2->parent->oldsched, a->tid);
			assert(a3 != NULL && "yielding thread vanished in the past");
			if (h2->chosen_thread == a->tid) {
				/* Thread ran. Count must have incremented. */
				if (a2->user_yield.loop_count == TOO_MANY_XCHGS) {
					/* No telling what count was before
					 * this transition. Stop checking. */
					break;
				} else {
					assert(a2->user_yield.loop_count ==
					       a3->user_yield.loop_count + 1);
					/* Update expected "future" count
					 * from the past. */
					expected_count--;
				}
			} else {
				/* Thread did not run; count should not have changed. */
				assert(a2->user_yield.loop_count ==
				       a3->user_yield.loop_count);
			}
		}
	}
}

bool is_user_yield_blocked(struct hax *h)
{
	struct agent *a = find_runnable_agent(h->oldsched, h->chosen_thread);
	return a != NULL && a->user_yield.blocked;
}

/******************** Scheduler-related ********************/

/* Called at the end of each transition. */
void check_user_yield_activity(struct user_sync_state *u, struct agent *a)
{
	struct user_yield_state *y = &a->user_yield;

	assert(y->loop_count >= 0);
	/* cannot be equal to the max, or we should not have run it. */
	assert((y->loop_count < TOO_MANY_YIELDS ||
	        y->loop_count == TOO_MANY_XCHGS) &&
	       "we accidentally ran a thread stuck in a yield loop");
	assert(!y->blocked);

	if (y->loop_count == TOO_MANY_XCHGS) {
		lsprintf(CHOICE, COLOUR_BOLD COLOUR_CYAN "TID %d looped around "
			 "xchg too many times, marking it blocked.\n", a->tid);
	} else if (u->yield_progress == YIELDED) {
		y->loop_count++;
		if (y->loop_count == TOO_MANY_YIELDS) {
			lsprintf(CHOICE, COLOUR_BOLD COLOUR_CYAN "TID %d yielded "
				 "too many times (%d), marking it blocked.\n",
				 a->tid, y->loop_count);
		} else {
			lsprintf(CHOICE, COLOUR_BOLD COLOUR_CYAN "TID %d seems to "
				 "be stuck in a yield loop (%d spins)\n",
				 a->tid, y->loop_count);
		}
	} else {
		assert(u->yield_progress == NOTHING_INTERESTING ||
		       u->yield_progress == ACTIVITY);
		if (y->loop_count > 0) {
			/* User thread was yielding but stopped. */
			lsprintf(DEV, COLOUR_BOLD COLOUR_CYAN "TID %d yielded "
				 "%d times, but stopped.\n",
				 a->tid, y->loop_count);
			y->loop_count = 0;
		} else {
			/* Normal activity, nothing to see here. */
		}
	}

	/* reset state for the next thread/transition to run. */
	u->yield_progress = NOTHING_INTERESTING;
	u->xchg_count = 0;
}

bool check_user_xchg(struct user_sync_state *u, struct agent *a)
{
	struct user_yield_state *y = &a->user_yield;
	assert(u->xchg_count >= 0);
	assert(u->xchg_count < TOO_MANY_XCHGS);
	/* We use TOO_MANY_XCHGS as a special value for the yield counter so
	 * we can piggy-back on the check-yield code. So we need to make sure
	 * we don't collide with the space of possible values the yield code
	 * might use, and also leave TOO_MANY_YIELDS + 1 free as an impossible
	 * value that we can assert on. (I wish I had sum types...) */
	STATIC_ASSERT(TOO_MANY_XCHGS > TOO_MANY_YIELDS + 1);

	u->xchg_count++;
	lsprintf(DEV, "user xchg TID %d (%d%s time)\n", a->tid, u->xchg_count,
		 u->xchg_count == 1 ? "st" : u->xchg_count == 2 ? "nd" :
		 u->xchg_count == 3 ? "rd" : "th");

	if (u->xchg_count == TOO_MANY_XCHGS) {
		/* Set up user yield state to make check_user_yield() treat
		 * this the same as if it yielded too many times (even if it had
		 * other activity - an xchg loop is blocked for sure). */
		y->loop_count = TOO_MANY_XCHGS;
		/* Transition ends. Reset global counter for the next one. */
		u->xchg_count = 0;
		return true;
	} else {
		return false;
	}
}

/* Called whenever the user hits an 'interesting' code point in userspace. */
void record_user_yield_activity(struct user_sync_state *u)
{
	/* Unconditionally indicate interesting activity. Even if the user
	 * thread yields during this transition, we'll wait for it to actually
	 * start doing nothing but yielding before counting it as blocked. */
	u->yield_progress = ACTIVITY;
	u->xchg_count = 0;
}

/* Called whenever the user makes a yield syscall. */
void record_user_yield(struct user_sync_state *u)
{
	/* If the user thread already did something interesting this
	 * transition, let this pass. Otherwise, start counting. */
	if (u->yield_progress == NOTHING_INTERESTING)
		u->yield_progress = YIELDED;
}

/******************** Memory-related ********************/

static void maybe_unblock(struct ls_state *ls, struct agent *a, unsigned int addr)
{
	bool found_one = false;

	/* Find all its past transitions (since it started yielding) */
	for (struct hax *h = ls->save.current; h->parent != NULL; h = h->parent) {
		if (h->chosen_thread != a->tid) {
			continue;
		}

		found_one = true;

		/* Check the loop count of the thread as it was BEFORE this
		 * transition started. We shouldn't check the first transition
		 * in the yield sequence, as it may have other unrelated shm
		 * accesses, so do this now before the real check below. */
		assert(h->parent != NULL && "reached root too soon");
		struct agent *a2 = find_runnable_agent(h->parent->oldsched, a->tid);
		assert(a2 != NULL);
		/* ...unless it became blocked in an xchg loop, in which case
		 * there won't be a chain of counting-up transitions. */
		if (a->user_yield.loop_count != TOO_MANY_XCHGS &&
		    a2->user_yield.loop_count == 0) {
			/* First time it was blocked. Don't continue. */
			break;
		}

		/* Actual check. Might the access cause it to stop yielding? */
		if (shm_contains_addr(h->old_user_mem, addr)) {
			lsprintf(DEV, "TID %d's write to 0x%x unblocks %s thread "
				 "#%d/tid%d\n", ls->sched.cur_agent->tid, addr,
				 a->user_yield.loop_count == TOO_MANY_XCHGS ?
				 "xchging" : "yielding", h->depth, a->tid);
			/* Mark the thread unblocked. */
			a->user_yield.loop_count = 0;
			/* This flag need not have been set, but it might have
			 * been from running DPOR on a past branch. */
			a->user_yield.blocked = false;
			break;
		}
	}
	assert(found_one);
}

/* Called for every userspace shared memory write by an active thread. */
void check_unblock_yield_loop(struct ls_state *ls, unsigned int addr)
{
	struct agent *a;
	FOR_EACH_RUNNABLE_AGENT(a, &ls->sched,
		if (a->tid != ls->sched.cur_agent->tid &&
		    (a->user_yield.loop_count == TOO_MANY_YIELDS ||
		    a->user_yield.loop_count == TOO_MANY_XCHGS)) {
			maybe_unblock(ls, a, addr);
		}
	);
}
