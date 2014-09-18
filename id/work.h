/**
 * @file work.h
 * @brief workqueue thread pool
 * @author Ben Blum
 */

#ifndef __ID_WORK_H
#define __ID_WORK_H

struct job;

void add_work(struct job *j);
void signal_work();
void start_work(unsigned long num_cpus);
void wait_to_finish_work();

#endif
