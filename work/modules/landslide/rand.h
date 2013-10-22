/**
 * @file rand.h
 * @brief Random number generation (mersenne twister)
 */

#ifndef __LS_RAND_H
#define __LS_RAND_H

#include <simics/api.h>

#define RAND_N 624

struct rand_state {
	unsigned long mt[RAND_N];
	int mti;
};

void rand_init(struct rand_state *r);
uint32_t rand32(struct rand_state *r);
uint64_t rand64(struct rand_state *r);

#endif
