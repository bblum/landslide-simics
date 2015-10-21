/**
 * @file time.c
 * @brief time to stop asking stupid questions.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <pthread.h>
#include <sys/time.h>

#include "common.h"
#include "sync.h"
#include "time.h"
#include "xcalls.h"

/* end time = start_timestamp + budget */
unsigned long start_timestamp = 0;
unsigned long budget = 0;

/* recording time spent actively using the cpu, rather than waiting for work */
struct cpu_time {
	unsigned long previous_total;
	/* wtb option types */
	bool running_now;
	unsigned long running_since;
};
static struct cpu_time *cpu_times = NULL;
static unsigned int num_cpus = 0;
static pthread_mutex_t cpu_time_lock = PTHREAD_MUTEX_INITIALIZER;

unsigned long timestamp()
{
	struct timeval tv;
	XGETTIMEOFDAY(&tv);
	return (unsigned long)((tv.tv_sec * 1000000) + tv.tv_usec);
}

void start_time(unsigned long usecs, unsigned int cpus)
{
	assert(start_timestamp == 0);
	assert(budget == 0);
	start_timestamp = timestamp();
	budget = usecs;
	cpu_times = XMALLOC(cpus, struct cpu_time);
	num_cpus = cpus;
	for (unsigned int i = 0; i < cpus; i++) {
		cpu_times[i].previous_total = 0;
		cpu_times[i].running_now = false;
	}
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

void start_using_cpu(unsigned int which)
{
	LOCK(&cpu_time_lock);
	assert(cpu_times != NULL);
	assert(which < num_cpus);
	assert(!cpu_times[which].running_now);
	cpu_times[which].running_since = timestamp();
	cpu_times[which].running_now = true;
	UNLOCK(&cpu_time_lock);
}

void stop_using_cpu(unsigned int which)
{
	LOCK(&cpu_time_lock);
	assert(cpu_times != NULL);
	assert(which < num_cpus);
	assert(cpu_times[which].running_now);
	cpu_times[which].previous_total +=
		timestamp() - cpu_times[which].running_since;
	cpu_times[which].running_now = false;
	UNLOCK(&cpu_time_lock);
}

unsigned long total_cpu_time()
{
	LOCK(&cpu_time_lock);
	assert(cpu_times != NULL);
	unsigned long now = timestamp();
	unsigned long total = 0;
	for (unsigned int i = 0; i < num_cpus; i++) {
		total += cpu_times[i].previous_total;
		if (cpu_times[i].running_now) {
			total += now - cpu_times[i].running_since;
		}
	}
	UNLOCK(&cpu_time_lock);
	return total;
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
