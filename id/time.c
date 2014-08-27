/**
 * @file time.c
 * @brief time to stop asking stupid questions.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <sys/time.h>

#include "common.h"
#include "time.h"

/* end time = start_timestamp + budget */
unsigned long start_timestamp = 0;
unsigned long budget = 0;

unsigned long timestamp()
{
	struct timeval tv;
	int rv = gettimeofday(&tv, NULL);
	assert(rv == 0 && "failed gettimeofday");
	return (unsigned long)((tv.tv_sec * 1000000) + tv.tv_usec);
}

void start_time(unsigned long usecs)
{
	assert(start_timestamp == 0);
	assert(budget == 0);
	start_timestamp = timestamp();
	budget = usecs;
}

unsigned long time_remaining()
{
	assert(start_timestamp != 0);
	unsigned long now = timestamp();
	unsigned long end_time = start_timestamp + budget;
	return end_time < now ? 0 : end_time - now;
}
