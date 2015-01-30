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

unsigned long time_elapsed()
{
	assert(start_timestamp != 0);
	unsigned long now = timestamp();
	assert(now >= start_timestamp && "travelled back in time??");
	return now - start_timestamp;
}

unsigned long time_remaining()
{
	assert(start_timestamp != 0);
	unsigned long now = timestamp();
	unsigned long end_time = start_timestamp + budget;
	return end_time < now ? 0 : end_time - now;
}

void human_friendly_time(long double usecs, struct human_friendly_time *hft)
{
	long double secs = usecs / 1000000;
	if ((hft->inf = (secs > (long double)UINT64_MAX))) {
		return;
	}

	hft->years = 0;
	hft->days = 0;
	hft->hours = 0;
	hft->mins = 0;
	hft->secs = (uint64_t)secs;
	if (hft->secs >= 60) {
		hft->mins = hft->secs / 60;
		hft->secs = hft->secs % 60;
	}
	if (hft->mins >= 60) {
		hft->hours = hft->mins / 60;
		hft->mins  = hft->mins % 60;
	}
	if (hft->hours >= 24) {
		hft->days  = hft->hours / 24;
		hft->hours = hft->hours % 24;
	}
	if (hft->days >= 365) {
		hft->years = hft->days / 365;
		hft->days  = hft->days % 365;
	}
}

void print_human_friendly_time(struct human_friendly_time *hft)
{
	if (hft->inf) {
		PRINT("INF");
		return;
	}

	if (hft->years != 0)
		PRINT("%luy ", hft->years);
	if (hft->days  != 0)
		PRINT("%lud ", hft->days);
	if (hft->hours != 0)
		PRINT("%luh ", hft->hours);
	if (hft->mins  != 0)
		PRINT("%lum ", hft->mins);

	PRINT("%lus", hft->secs);
}

void dbg_human_friendly_time(struct human_friendly_time *hft)
{
	if (hft->inf) {
		DBG("INF");
		return;
	}

	if (hft->years != 0)
		DBG("%luy ", hft->years);
	if (hft->days  != 0)
		DBG("%lud ", hft->days);
	if (hft->hours != 0)
		DBG("%luh ", hft->hours);
	if (hft->mins  != 0)
		DBG("%lum ", hft->mins);

	DBG("%lus", hft->secs);
}
