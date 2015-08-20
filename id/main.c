/**
 * @file main.c
 * @brief iterative deepening framework for landslide
 * @author Ben Blum
 */

#define _XOPEN_SOURCE 700

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "bug.h"
#include "common.h"
#include "job.h"
#include "option.h"
#include "pp.h"
#include "signals.h"
#include "time.h"
#include "work.h"

bool control_experiment;
unsigned long eta_factor;
unsigned long eta_threshold;

int main(int argc, char **argv)
{
	char test_name[BUF_SIZE];
	unsigned long max_time;
	unsigned long num_cpus;
	bool verbose;
	bool leave_logs;
	bool use_wrapper_log;
	char wrapper_log[BUF_SIZE];
	bool pintos;
	unsigned long progress_interval;

	if (!get_options(argc, argv, test_name, BUF_SIZE, &max_time, &num_cpus,
			 &verbose, &leave_logs, &control_experiment,
			 &use_wrapper_log, wrapper_log, BUF_SIZE, &pintos,
			 &progress_interval, &eta_factor, &eta_threshold)) {
		usage(argv[0]);
		exit(ID_EXIT_USAGE);
	}

	set_logging_options(use_wrapper_log, wrapper_log);

	DBG("will run for at most %lu seconds\n", max_time);

	set_job_options(test_name, verbose, leave_logs, pintos);
	init_signal_handling();
	start_time(max_time * 1000000);

	if (!control_experiment) {
		add_work(new_job(create_pp_set(PRIORITY_NONE), true));
		add_work(new_job(create_pp_set(PRIORITY_MUTEX_LOCK), true));
		add_work(new_job(create_pp_set(PRIORITY_MUTEX_UNLOCK), true));
	}
	add_work(new_job(create_pp_set(PRIORITY_MUTEX_LOCK | PRIORITY_MUTEX_UNLOCK), true));
	start_work(num_cpus, progress_interval);
	wait_to_finish_work();
	print_live_data_race_pps();

	return found_any_bugs() ? ID_EXIT_BUG_FOUND : ID_EXIT_SUCCESS;
}
