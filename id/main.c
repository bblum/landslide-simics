/**
 * @file main.c
 * @brief iterative deepening framework for landslide
 * @author Ben Blum
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "common.h"
#include "job.h"
#include "pp.h"
#include "time.h"

#define MINTIME ((unsigned long)600) /* 10 mins */
#define DEFAULT_TIME ((unsigned long)3600) /* 1hr */

#define DEFAULT_TEST_CASE "thread_exit_join"

void usage(char *execname)
{
	ERR("Usage: %s [maxtime]\n", execname);
	ERR("maxtime in positive seconds, or can be suffixed with m/h/d/y\n");
}

bool parse_time(char *str, unsigned long *result)
{
	char *endp;
	long time = strtol(str, &endp, 0);
	if (errno != 0) {
		ERR("Time must be a number '%s'\n", str);
		return false;
	}
	if (time < 0) {
		ERR("Cannot time travel\n");
		return false;
	}
	*result = (unsigned long)time;
	switch (*endp) {
		case 'y':
			WARN("%ld year%s, are you sure?\n", *result,
			     *result == 1 ? "" : "s");
			*result *= 365;
		case 'd':
			*result *= 24;
		case 'h':
			*result *= 60;
		case 'm':
			*result *= 60;
		case '\0':
		case 's':
			break;
		default:
			ERR("Unrecognized time format '%s'\n", str);
			return false;
	}
	if (*result < MINTIME) {
		WARN("%ld (%s) not enough time; defaulting to %ld\n",
		     *result, str, MINTIME);
		*result = MINTIME;
	}
	return true;
}

int main(int argc, char **argv)
{
	unsigned long time;

	// TODO: argument for test case name
	if (argc < 2) {
		time = DEFAULT_TIME;
		assert(time < ULONG_MAX / 1000000);
		WARN("maxtime not specified; defaulting to %lu seconds\n", time);
	} else {
		if (!parse_time(argv[1], &time)) {
			usage(argv[0]);
			return -1;
		}

		if (time >= ULONG_MAX / 1000000) {
			ERR("%ld seconds is too much time for unsigned long\n",
			    time);
			usage(argv[0]);
			return -1;
		}

		printf("will run for at most %lu seconds\n", time);
	}

	start_time(time * 1000000);

	struct job *j = new_job(create_pp_set(PRIORITY_ALL));
	start_job(j);
	finish_job(j);

	// TODO: return something better
	return 0;
}
