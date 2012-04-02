/**
 * @file vanish_vanish.c
 * @brief A test for vanish/vanish races.
 *
 * **> Public: Yes
 * **> Covers: fork, vanish
 * **> NeedsWork: Yes
 * **> For: P3
 * **> Authors: bblum
 * **> Notes: For landslide
 *
 * @author Ben Blum (bblum)
 * @bug No known bugs.
 */

#include <syscall.h>
#include <simics.h>
#include <stdio.h>

#include "410_tests.h"
DEF_TEST_NAME("vanish_vanish:");

/**
 * @brief Spawns the thread and attempts to join
 *
 * @param argc The number of arguments
 * @param argv The argument array
 * @return 1 on success, < 0 on error.
 */
int main(int argc, char *argv[])
{
	int pid;

	REPORT_START_CMPLT;

	pid = fork();
	if (pid < 0) {
		printf("fork failed\n");
		REPORT_END_FAIL;
		return -40;
	} else if (pid == 0) {
		/* child vanish */
		/* TODO: fork another child and vanish that too? */
		return 0x15410FA1L;
	} else {
		/* parent vanish */
		return 1;
	}
}
