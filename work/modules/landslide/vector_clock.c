/**
 * @file vector_clock.c
 * @brief Data structures for more different data race detection
 * @author Ben Blum
 */

#define MODULE_NAME "VC"
#define MODULE_COLOUR COLOUR_DARK COLOUR_BLUE

#include "common.h"
#include "vector_clock.h"

/******************************************************************************
 * Vector clock manipulation
 ******************************************************************************/

/* VCs are maps from tid to timestamp, but as a fast path, we will just use
 * the tid as an index into the array list. (IOW, v[tid].tid != tid, then
 * we will do slow-path stuff like O(n) scanning or resizing the array list.
 * Initial size is chosen so none of the recommended test cases create tids
 * higher than it. */
#define VC_INIT_SIZE 8

void vc_init(struct vector_clock *vc)
{
	ARRAY_LIST_INIT(&vc->v, VC_INIT_SIZE);
	for (int i = 0; i < VC_INIT_SIZE; i++) {
		struct epoch bottom = { .tid = i, .timestamp = 0 };
		ARRAY_LIST_APPEND(&vc->v, bottom);
	}
}

/* don't pass something already inited for vc_new, or this will leak!  */
void vc_copy(struct vector_clock *vc_new, const struct vector_clock *vc_existing)
{
	unsigned int i;
	struct epoch *e;
	ARRAY_LIST_INIT(&vc_new->v, ARRAY_LIST_SIZE(&vc_existing->v));
	ARRAY_LIST_FOREACH(&vc_existing->v, i, e) {
		ARRAY_LIST_APPEND(&vc_new->v, *e);
	}
}

static bool vc_find(struct vector_clock *vc, unsigned int tid, struct epoch **e)
{
	/* fast path: try find tid at vc[tid] */
	if (tid < VC_INIT_SIZE) {
		*e = ARRAY_LIST_GET(&vc->v, tid);
		assert((*e)->tid == tid);
		return true;
	}

	/* slow path: tid too large; look for it in a non-reserved slot */
	unsigned int i;
	ARRAY_LIST_FOREACH(&vc->v, i, *e) {
		if ((*e)->tid == tid) {
			/* slow path 1: found it in some noob slot */
			assert(i >= VC_INIT_SIZE);
			return true;
		}
	}
	/* slow path 2: no entry exists for tid */
	*e = NULL;
	return false;
}

void vc_inc(struct vector_clock *vc, unsigned int tid)
{
	struct epoch *e;
	if (vc_find(vc, tid, &e)) {
		e->timestamp++;
	} else {
		struct epoch new_epoch = { .tid = tid, .timestamp = 1 };
		ARRAY_LIST_APPEND(&vc->v, new_epoch);
	}
}

/* 0 is the bottom value; if a thread was ever inced in this vc, its timestamp
 * will be at least 1. */
unsigned int vc_get(struct vector_clock *vc, unsigned int tid)
{
	struct epoch *e;
	if (vc_find(vc, tid, &e)) {
		return e->timestamp;
	} else {
		return 0;
	}
}

/* vc_dest's values are changed, vc_src's are not */
void vc_merge(struct vector_clock *vc_dest, struct vector_clock *vc_src)
{
	unsigned int i;
	struct epoch *e_dest;
	struct epoch *e_src;

	/* step 1: anything that vc_dest has, find it in vc_src and merge it */
	ARRAY_LIST_FOREACH(&vc_dest->v, i, e_dest) {
		if (vc_find(vc_src, e_dest->tid, &e_src)) {
			e_dest->timestamp =
				MAX(e_dest->timestamp, e_src->timestamp);
		}
	}

	/* step 2: anything that vc_dest was missing, copy vc_src's entry */
	if (ARRAY_LIST_SIZE(&vc_src->v) >= VC_INIT_SIZE) {
		ARRAY_LIST_FOREACH(&vc_src->v, i, e_src) {
			if (!vc_find(vc_dest, e_src->tid, &e_dest)) {
				/* all fast-path slots should be found in step 1 */
				assert(i >= VC_INIT_SIZE);
				assert(e_src->tid >= VC_INIT_SIZE);
				ARRAY_LIST_APPEND(&vc_dest->v, *e_src);
			}
		}
	}
}

bool vc_eq(struct vector_clock *vc1, struct vector_clock *vc2)
{
	unsigned int i;
	struct epoch *e1;
	struct epoch *e2;

	ARRAY_LIST_FOREACH(&vc1->v, i, e1) {
		if (vc_find(vc2, e1->tid, &e2)) {
			if (e1->timestamp != e2->timestamp) {
				return false;
			}
		} else if (e1->timestamp != 0) {
			return false;
		}
	}

	/* any elements in v2 not present in v1 at all? */
	if (ARRAY_LIST_SIZE(&vc2->v) >= VC_INIT_SIZE) {
		ARRAY_LIST_FOREACH(&vc2->v, i, e2) {
			/* if it exists, it was already confirmed equal */
			if (!vc_find(vc1, e2->tid, &e1)) {
				return false;
			}
		}
	}

	return true;
}

bool vc_happens_before(struct vector_clock *vc_before, struct vector_clock *vc_after)
{
	unsigned int i;
	struct epoch *e_before;
	struct epoch *e_after;

	/* step 1: anything that vc_before has, compare to vc_after's version */
	ARRAY_LIST_FOREACH(&vc_before->v, i, e_before) {
		if (vc_find(vc_after, e_before->tid, &e_after)) {
			/* Note use of ">" not ">=". HB is ok if two timestamps
			 * are equal. (see fasttrack paper, sec 2.2) */
			if (e_before->timestamp > e_after->timestamp) {
				return false;
			}
		} else if (e_before->timestamp != 0) {
			/* First vector has an event from this thread, but
			 * second's value is bottom. No HB here, officer! */
			return false;
		}
	}

	/* step 2: any elements vc_before was missing, their timestamps are all
	 * bottom, and automatically compare OK to new entries in vc_after. */
	return true;
}

void vc_print(verbosity v, struct vector_clock *vc)
{
	unsigned int i;
	struct epoch *e;
	bool first = true;

	printf(v, "[");
	ARRAY_LIST_FOREACH(&vc->v, i, e) {
		if (!first) {
			printf(v, ", ");
		}
		printf(v, "%u@%u", e->tid, e->timestamp);
		first = false;
	}
	printf(v, "]");
}

#if 0
void vc_test()
{
	struct vector_clock a;
	struct vector_clock b;
	struct epoch *e;

	/* test inc/get/etc */
	vc_init(&a); vc_init(&b);
	assert(vc_find(&a, VC_INIT_SIZE - 1, &e));
	assert(!vc_find(&a, VC_INIT_SIZE, &e));
	assert(vc_happens_before(&a, &b));
	assert(vc_happens_before(&b, &a));

	vc_inc(&a, 0);
	assert(vc_get(&a, 0) == 1);
	vc_inc(&a, VC_INIT_SIZE);
	vc_inc(&a, VC_INIT_SIZE);
	assert(vc_find(&a, VC_INIT_SIZE, &e));
	assert(vc_get(&a, VC_INIT_SIZE) == 2);

	assert(!vc_happens_before(&a, &b));
	assert(vc_happens_before(&b, &a));
	vc_inc(&b, 1);
	assert(!vc_happens_before(&b, &a));
	vc_destroy(&a); vc_destroy(&b);

	/* test merge */
	vc_init(&a); vc_init(&b);
	vc_inc(&a, 0);
	vc_inc(&a, 1);
	vc_inc(&b, 1);
	vc_inc(&b, 1);
	vc_inc(&a, VC_INIT_SIZE);
	vc_inc(&b, VC_INIT_SIZE+1);
	assert(vc_get(&a, VC_INIT_SIZE+1) == 0);

	vc_merge(&a, &b);
	assert(vc_get(&a, 0) == 1);
	assert(vc_get(&a, 1) == 2);
	assert(vc_get(&a, VC_INIT_SIZE) == 1);
	assert(vc_get(&a, VC_INIT_SIZE+1) == 1);
	assert(vc_happens_before(&b, &a));
	assert(!vc_happens_before(&a, &b));

	vc_merge(&b, &a);
	assert(vc_get(&b, VC_INIT_SIZE) == 1);
	vc_destroy(&a); vc_destroy(&b);

	/* test hb matching elements */
	vc_init(&a); vc_init(&b);
	vc_inc(&a, 4);
	vc_inc(&b, 4);
	vc_inc(&b, 4);
	vc_inc(&a, VC_INIT_SIZE+2);
	vc_inc(&b, VC_INIT_SIZE+2);
	vc_inc(&b, VC_INIT_SIZE+2);
	assert(vc_happens_before(&a, &b));
	assert(!vc_happens_before(&b, &a));
	vc_destroy(&a); vc_destroy(&b);

	/* test hb element vs bottom */
	vc_init(&a); vc_init(&b);
	vc_inc(&b, 4);
	vc_inc(&a, VC_INIT_SIZE+2);
	vc_inc(&b, VC_INIT_SIZE+2);
	vc_inc(&b, VC_INIT_SIZE+2);
	assert(vc_happens_before(&a, &b));
	assert(!vc_happens_before(&b, &a));
	vc_destroy(&a); vc_destroy(&b);

	/* test hb double bottom (false) */
	vc_init(&a); vc_init(&b);
	vc_inc(&b, 4);
	vc_inc(&a, VC_INIT_SIZE+2);
	assert(!vc_happens_before(&a, &b));
	assert(!vc_happens_before(&b, &a));
	vc_destroy(&a); vc_destroy(&b);

	assert(false && "all tests passed!");
}
#endif

/******************************************************************************
 * Lock clock map
 ******************************************************************************/

struct lock_clock {
	unsigned int lock_addr;
	struct vector_clock c;
	struct rb_node nobe;
};

/* "ls" is kind of already a reserved variable name */
void lock_clocks_init(struct lock_clocks *lc)
{
	lc->map.rb_node = NULL;
	lc->num_lox = 0;
}

static void free_lock_clocks(struct rb_node *nobe)
{
	if (nobe == NULL) {
		return;
	}

	free_lock_clocks(nobe->rb_left);
	free_lock_clocks(nobe->rb_right);

	struct lock_clock *clock = rb_entry(nobe, struct lock_clock, nobe);
	vc_destroy(&clock->c);
	MM_FREE(clock);
}

void lock_clocks_destroy(struct lock_clocks *lc)
{
	free_lock_clocks(lc->map.rb_node);
	lc->map.rb_node = NULL;
	lc->num_lox = 0;
}

static struct rb_node *dup_clock(const struct rb_node *nobe,
				 const struct rb_node *parent)
{
	if (nobe == NULL) {
		return NULL;
	}

	struct lock_clock *src = rb_entry(nobe, struct lock_clock, nobe);
	struct lock_clock *dest = MM_XMALLOC(1, struct lock_clock);

	int colour_flag = src->nobe.rb_parent_color & 1;
	assert(((unsigned long)parent & 1) == 0);
	dest->nobe.rb_parent_color = (unsigned long)parent | colour_flag;
	dest->nobe.rb_right = dup_clock(src->nobe.rb_right, &dest->nobe);
	dest->nobe.rb_left  = dup_clock(src->nobe.rb_left,  &dest->nobe);

	dest->lock_addr = src->lock_addr;
	vc_copy(&dest->c, &src->c);

	return &dest->nobe;
}

void lock_clocks_copy(struct lock_clocks *lm_new, const struct lock_clocks *lm_existing)
{
	lm_new->map.rb_node = dup_clock(lm_existing->map.rb_node, NULL);
	lm_new->num_lox = lm_existing->num_lox;
}

/* see function of same name in memory.c...
 * returns NULL if lock_addr already exists; if so, chunk will point to it */
static struct rb_node **find_insert_location(struct rb_root *root,
					     unsigned int lock_addr,
					     struct lock_clock **clock)
{
	struct rb_node **p = &root->rb_node;

	while (*p) {
		*clock = rb_entry(*p, struct lock_clock, nobe);

		/* Both same lock type; branch based on addr comparison */
		if (lock_addr < (*clock)->lock_addr) {
			p = &(*p)->rb_left;
		} else if (lock_addr > (*clock)->lock_addr) {
			p = &(*p)->rb_right;
		/* Found a match */
		} else {
			return NULL;
		}
	}

	return p;
}

bool lock_clock_find(struct lock_clocks *lc, unsigned int lock_addr,
		     struct vector_clock **result)
{
	struct lock_clock *entry;
	struct rb_node **p = find_insert_location(&lc->map, lock_addr, &entry);
	if (p == NULL) {
		assert(entry != NULL && "find insert location is broken?");
		*result = &entry->c;
		return true;
	} else {
		return false;
	}
}

struct vector_clock *lock_clock_get(struct lock_clocks *lc, unsigned int lock_addr)
{
	struct vector_clock *result;
	bool found = lock_clock_find(lc, lock_addr, &result);
	assert(found && "time to get a watch!");
	return result;
}

void lock_clock_set(struct lock_clocks *lc, unsigned int lock_addr,
		    struct vector_clock *vc)
{
	struct lock_clock *parent = NULL;
	struct rb_node **p = find_insert_location(&lc->map, lock_addr, &parent);
	if (p == NULL) {
		/* lock already had a clock */
		assert(parent->lock_addr == lock_addr);
		vc_destroy(&parent->c);
		vc_copy(&parent->c, vc);
	} else {
		/* insert a fresh one */
		struct lock_clock *entry = MM_XMALLOC(1, struct lock_clock);
		entry->lock_addr = lock_addr;
		vc_copy(&entry->c, vc);

		rb_init_node(&entry->nobe);
		rb_link_node(&entry->nobe, parent != NULL ? &parent->nobe : NULL, p);
		rb_insert_color(&entry->nobe, &lc->map);
		lc->num_lox++;
	}
}

#if 0
void test_lock_clocks()
{
	struct vector_clock va, vb, vc, vd;
	struct vector_clock *vp;
	vc_init(&va); vc_init(&vb); vc_init(&vc); vc_init(&vd);
	vc_inc(&va, 1);
	vc_inc(&vb, 2); vc_inc(&vb, 2);
	vc_inc(&vc, 3); vc_inc(&vc, 3); vc_inc(&vc, 3);
	vc_inc(&vd, 4); vc_inc(&vd, 4); vc_inc(&vd, 4); vc_inc(&vd, 4);

	struct lock_clocks lc;
	lock_clocks_init(&lc);
	assert(!lock_clock_find(&lc, 0x15410de0u, &vp));

	lock_clock_set(&lc, 0x15410de0u, &va);
	assert(lc.num_lox == 1);
	lock_clock_set(&lc, 0x1badb002, &vb);
	assert(lc.num_lox == 2);
	lock_clock_set(&lc, 0xcdcdcdcd, &vc);
	assert(lc.num_lox == 3);
	assert(lock_clock_find(&lc, 0x15410de0u, &vp));
	assert(vc_get(vp, 1) == 1);
	assert(vc_get(vp, 2) == 0);
	assert(vc_get(vp, 3) == 0);
	assert(vc_get(vp, 4) == 0);
	assert(lock_clock_find(&lc, 0x1badb002, &vp));
	assert(vc_get(vp, 1) == 0);
	assert(vc_get(vp, 2) == 2);
	assert(vc_get(vp, 3) == 0);
	assert(vc_get(vp, 4) == 0);
	assert(lock_clock_find(&lc, 0xcdcdcdcd, &vp));
	assert(vc_get(vp, 1) == 0);
	assert(vc_get(vp, 2) == 0);
	assert(vc_get(vp, 3) == 3);
	assert(vc_get(vp, 4) == 0);

	lock_clock_set(&lc, 0x1badb002, &vd);
	assert(lc.num_lox == 3); /* overwrite same lock */
	assert(lock_clock_find(&lc, 0x1badb002, &vp));
	assert(vc_get(vp, 1) == 0);
	assert(vc_get(vp, 2) == 0);
	assert(vc_get(vp, 3) == 0);
	assert(vc_get(vp, 4) == 4);

	assert(false && "all tests passed!");
}
#endif
