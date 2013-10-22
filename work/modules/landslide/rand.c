/* A C-program for MT19937: Integer     version                   */
/*  genrand() generates one pseudorandom unsigned integer (32bit) */
/* which is uniformly distributed among 0 to 2^32-1  for each     */
/* call. sgenrand(seed) set initial values to the working area    */
/* of 624 words. Before genrand(), sgenrand(seed) must be         */
/* called once. (seed is any 32-bit integer except for 0).        */
/*   Coded by Takuji Nishimura, considering the suggestions by    */
/* Topher Cooper and Marc Rieffel in July-Aug. 1997.              */

/* This library is free software; you can redistribute it and/or   */
/* modify it under the terms of the GNU Library General Public     */
/* License as published by the Free Software Foundation; either    */
/* version 2 of the License, or (at your option) any later         */
/* version.                                                        */
/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.            */
/* See the GNU Library General Public License for more details.    */
/* You should have received a copy of the GNU Library General      */
/* Public License along with this library; if not, write to the    */
/* Free Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA   */ 
/* 02111-1307  USA                                                 */

/* Copyright (C) 1997 Makoto Matsumoto and Takuji Nishimura.       */
/* Any feedback is very welcome. For any question, comments,       */
/* see http://www.math.keio.ac.jp/matumoto/emt.html or email       */
/* matumoto@math.keio.ac.jp                                        */

/* modified for 15-410 at CMU by Zachary Anderson(zra) */

/* modified for Landslide at CMU by Ben Blum (bblum) */

#include <sys/time.h>

#define MODULE_NAME "RAND"

#include "common.h"
#include "rand.h"

/* Period parameters */  
#define N RAND_N
#define M 397
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/* Tempering parameters */   
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

// these are now in the rand_state struct
// static unsigned long r->mt[N]; /* the array for the state vector  */
// static int mti=N+1; /* mti==N+1 means r->mt[N] is not initialized */

/* initializing the array with a NONZERO seed */
void
sgenrand(struct rand_state *r, unsigned long seed)
{
    /* setting initial seeds to r->mt[N] using         */
    /* the generator Line 25 of Table 1 in          */
    /* [KNUTH 1981, The Art of Computer Programming */
    /*    Vol. 2 (2nd Ed.), pp102]                  */
    r->mt[0]= seed & 0xffffffff;
    for (r->mti=1; r->mti<N; r->mti++)
        r->mt[r->mti] = (69069 * r->mt[r->mti-1]) & 0xffffffff;
}

unsigned long 
genrand(struct rand_state *r)
{
    unsigned long y;
    static unsigned long mag01[2]={0x0, MATRIX_A};
    /* mag01[x] = x * MATRIX_A  for x=0,1 */

    if (r->mti >= N) { /* generate N words at one time */
        int kk;

        assert(r->mti == N && "RNG corrupted");
        // if (r->mti == N+1)   /* if sgenrand() has not been called, */
        //     sgenrand(4357); /* a default initial seed is used   */

        for (kk=0;kk<N-M;kk++) {
            y = (r->mt[kk]&UPPER_MASK)|(r->mt[kk+1]&LOWER_MASK);
            r->mt[kk] = r->mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        for (;kk<N-1;kk++) {
            y = (r->mt[kk]&UPPER_MASK)|(r->mt[kk+1]&LOWER_MASK);
            r->mt[kk] = r->mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
        }
        y = (r->mt[N-1]&UPPER_MASK)|(r->mt[0]&LOWER_MASK);
        r->mt[N-1] = r->mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

        r->mti = 0;
    }
  
    y = r->mt[r->mti++];
    y ^= TEMPERING_SHIFT_U(y);
    y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
    y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
    y ^= TEMPERING_SHIFT_L(y);

    return y; 
}


void rand_init(struct rand_state *r)
{
	struct timeval tv;
	int ret = gettimeofday(&tv, NULL);
	assert(ret == 0 && "failed to seed RNG");
	sgenrand(r, tv.tv_sec ^ tv.tv_usec);
}

uint32_t rand32(struct rand_state *r)
{
	return (uint32_t)genrand(r);
}

uint64_t rand64(struct rand_state *r)
{
	uint64_t a = rand32(r);
	uint64_t b = rand32(r);
	return a | (b << 32);
}
