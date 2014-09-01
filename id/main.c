/**
 * @file main.c
 * @brief iterative deepening framework for landslide
 * @author Ben Blum
 */

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "common.h"
#include "found_a_bug.h"
#include "job.h"
#include "option.h"
#include "pp.h"
#include "time.h"

int main(int argc, char **argv)
{
	char test_name[BUF_SIZE];
	unsigned long max_time;
	unsigned long num_cpus;
	bool verbose;

	if (!get_options(argc, argv, test_name, BUF_SIZE,
			 &max_time, &num_cpus, &verbose)) {
		usage(argv[0]);
		exit(-1);
	}

	printf("will run for at most %lu seconds\n", max_time);

	start_time(max_time * 1000000);

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
			print_pp_set(config);
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

	return found_any_bugs() ? -1 : 0;
}
