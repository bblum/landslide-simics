/**
 * @file time.h
 * @brief time to stop asking stupid questions.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_TIME_H
#define __ID_TIME_H

#include <inttypes.h>

unsigned long timestamp();

void start_time(unsigned long usecs, unsigned int cpus);
unsigned long time_elapsed();
unsigned long time_remaining();

#define TIME_UP() (time_remaining() == 0)

void start_using_cpu(unsigned int which);
void stop_using_cpu(unsigned int which);
unsigned long total_cpu_time();

struct human_friendly_time { uint64_t secs, mins, hours, days, years; bool inf; };
void human_friendly_time(long double usecs, struct human_friendly_time *hft);
void print_human_friendly_time(struct human_friendly_time *hft);
void dbg_human_friendly_time(struct human_friendly_time *hft);

#endif
