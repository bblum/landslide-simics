/**
 * @file option.h
 * @brief command line options
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_OPTION_H
#define __ID_OPTION_H

void usage(char *execname);

bool get_options(int argc, char **argv, char *test_name, unsigned int test_name_len,
		 unsigned long *max_time, unsigned long *num_cpus, bool *verbose,
		 bool *leave_logs, bool *control_experiment, bool *use_wrapper_log,
		 char *wrapper_log, unsigned int wrapper_log_len,
		 unsigned long *progress_report_interval);

#endif
