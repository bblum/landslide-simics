/**
 * @file signals.c
 * @brief handling ^C etc
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "common.h"
#include "pp.h"
#include "signals.h"

static void handle_sigint(int MAYBE_UNUSED signum)
{
	pid_t me = syscall(SYS_gettid);
	DBG("ctrl-C press handled by thread %u\n", me);
	ERR("ctrl-C pressed, aborting...\n");
	try_print_live_data_race_pps();
	WARN("\n");
	WARN("some landslide processes may be left hanging; please 'killall simics-common'.\n");
	exit(ID_EXIT_CRASH);
}

void init_signal_handling()
{
	struct sigaction act;
	act.sa_handler = handle_sigint;
	sigemptyset(&act.sa_mask);
	int ret = sigaction(SIGINT, &act, NULL);
	assert(ret == 0 && "failed init signal handling");
	pid_t me = syscall(SYS_gettid);
	DBG("signal handling inited by thread %u\n", me);
}
