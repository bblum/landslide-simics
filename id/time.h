/**
 * @file time.h
 * @brief time to stop asking stupid questions.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_TIME_H
#define __ID_TIME_H

unsigned long timestamp();

void start_time(unsigned long usecs);
unsigned long time_remaining();

#define TIME_UP() (time_remaining() == 0)

#endif
