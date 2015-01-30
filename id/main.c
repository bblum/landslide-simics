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

int main(int argc, char **argv)
{
	char test_name[BUF_SIZE];
	unsigned long max_time;
	unsigned long num_cpus;
	bool verbose;
	bool leave_logs;

	if (!get_options(argc, argv, test_name, BUF_SIZE, &max_time, &num_cpus,
			 &verbose, &leave_logs, &control_experiment)) {
		usage(argv[0]);
		exit(-1);
	}

	DBG("will run for at most %lu seconds\n", max_time);

	set_job_options(test_name, verbose, leave_logs);
	init_signal_handling();
	start_time(max_time * 1000000);

	if (!control_experiment) {
		add_work(new_job(create_pp_set(PRIORITY_NONE), true));
		add_work(new_job(create_pp_set(PRIORITY_MUTEX_LOCK), true));
		add_work(new_job(create_pp_set(PRIORITY_MUTEX_UNLOCK), true));
	}
	add_work(new_job(create_pp_set(PRIORITY_MUTEX_LOCK | PRIORITY_MUTEX_UNLOCK), true));
	start_work(num_cpus);
	wait_to_finish_work();

#if 0
	unsigned int hardcoded_configs[] = {
		PRIORITY_NONE,
		PRIORITY_MUTEX_LOCK,
		PRIORITY_MUTEX_UNLOCK,
		PRIORITY_MUTEX_LOCK | PRIORITY_MUTEX_UNLOCK,
	};

	/* Try each hardcoded config in order, adding more data race PPs until
	 * the data races are saturated, then move to the next config option. */
	for (unsigned int i = 0; i < ARRAY_SIZE(hardcoded_configs); i++) {
		/* Always reuse data race PPs found from past configs. */
		unsigned int mask = hardcoded_configs[i] |
			PRIORITY_DR_CONFIRMED | PRIORITY_DR_SUSPECTED;
		struct pp_set *config = create_pp_set(mask);

		while (true) {
			/* Can we safely skip this redundant PP set? */
			if (bug_already_found(config)) {
				break;
			}

			struct job *j = new_job(config);
			unsigned int last_generation = j->generation;
			printf("Starting job with PP set ");
			print_pp_set(config, false);
			printf("\n");
			start_job(j);
			finish_job(j);
			// TODO: check bug found?

			/* Did new data races appear? */
			config = create_pp_set(mask);
			if (last_generation == compute_generation(config)) {
				/* No. Advance to next hardcoded config. */
				break;
			}

			if (TIME_UP()) { WARN("timeup\n"); break; }
		}

		free_pp_set(config);

		// TODO: check bug found
		if (TIME_UP()) { WARN("timeup\n"); break; }
	}
#endif

	return found_any_bugs() ? -1 : 0;
}
