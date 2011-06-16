/**
 * @file sp_table.c
 * @author Ben Blum <bblum@andrew.cmu.edu>
 * @brief Records stack pointers to keep track of which threads have had
 *        interrupts already triggered on them upon return.
 */

#include <assert.h>

#include <simics/alloc.h>

#include "sp_table.h"

static int sp_hash(int esp) {
	int x = LANDSLIDE_NUM_SPS;
	return (esp % x) ^ ((esp / x) % x) ^ ((esp / (x * x)) % x);
}

void sp_add(struct sp_table *t, int esp) {
	struct sp_entry **bucket = &t->a[sp_hash(esp)];
	
	struct sp_entry *new = MM_MALLOC(1, struct sp_entry);
	assert(new != NULL && "alloc failed in sp_add");

	new->esp = esp;
	new->next = *bucket;
	*bucket = new;
}

int sp_remove(struct sp_table *t, int esp) {
	struct sp_entry **bucket = &t->a[sp_hash(esp)];

	while (*bucket) {
		if ((*bucket)->esp == esp) {
			struct sp_entry *tmp = *bucket;

			*bucket = (*bucket)->next;
			MM_FREE(tmp);
			return 1;
		}
		bucket = &((*bucket)->next);
	}

	/* reached end of chain, sp not found */
	return 0;
}
