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
bool should_work_block(struct job *j);
void start_work(unsigned long num_cpus, unsigned long progress_report_interval);
void wait_to_finish_work();

#endif
