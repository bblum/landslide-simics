/**
 * @file main.c
 * @brief iterative deepening framework for landslide
 * @author Ben Blum
 */

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "common.h"
#include "pp.h"
#include "job.h"

#define MINTIME ((long)600) /* 10 mins */

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
		return -1;
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
	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	// TODO: default arguments (60min?)
	// TODO: argument for test case name

	unsigned long time;
	if (!parse_time(argv[1], &time)) {
		usage(argv[0]);
		return -1;
	}

	printf("run for %ld seconds\n", time);

	struct job *j = new_job(create_pp_set(PRIORITY_ALL));
	start_job(j);
	finish_job(j);

	// TODO: return something better
	return 0;
}
