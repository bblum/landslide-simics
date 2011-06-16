/**
 * @file sp_table.c
 * @author Ben Blum <bblum@andrew.cmu.edu>
 * @brief Records stack pointers to keep track of which threads have had
 *        interrupts already triggered on them upon return.
 */

#ifndef LANDSLIDE_SP_TABLE_H
#define LANDSLIDE_SP_TABLE_H

#include <stdlib.h>

#define LANDSLIDE_NUM_SPS 4096 /* approx. max number of kthreads */

struct sp_entry {
	int esp;
	struct sp_entry *next;
};

struct sp_table {
	struct sp_entry *a[LANDSLIDE_NUM_SPS];
};

#define sp_init(t) memset(t, 0, sizeof(*t))

void sp_add(struct sp_table *, int esp);
int sp_remove(struct sp_table *, int esp); /* 1 if sp was present */

#endif
