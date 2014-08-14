/**
 * @file main.c
 * @brief iterative deepening framework for landslide
 * @author Ben Blum
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "common.h"

#define MINTIME ((long)600) /* 10 mins */

void usage(char *execname)
{
	ERR("Usage: %s [maxtime]\n", execname);
	ERR("maxtime in positive seconds, or can be suffixed with m/h/d/y\n");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	/* Interpret maxtime argument. */
	char *endp;
	long time = strtol(argv[1], &endp, 0);
	if (errno != 0) {
		ERR("Time must be a number '%s'\n", argv[1]);
		usage(argv[0]);
		return -1;
	}
	if (time < 0) {
		ERR("Cannot time travel\n");
		usage(argv[0]);
		return -1;
	}
	switch (*endp) {
		case 'y':
			WARN("%ld years, are you sure?\n", time);
			time *= 365;
		case 'd':
			time *= 24;
		case 'h':
			time *= 60;
		case 'm':
			time *= 60;
		case '\0':
		case 's':
			break;
		default:
			ERR("Unrecognized time format '%s'\n", argv[1]);
			usage(argv[0]);
			return -1;
	}
	if (time < MINTIME) {
		WARN("%ld (%s) not enough time; defaulting to %ld\n",
		     time, argv[1], MINTIME);
		time = MINTIME;
	}

	printf("run for %ld seconds\n", time);
}
