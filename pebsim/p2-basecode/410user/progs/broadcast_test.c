/** @file 410user/progs/broadcast-test.c
 *  @author bblum
 *  @brief tests broadcast in a landslide-friendly way
 *  @public yes
 *  @for p2
 *  @covers cond_wait,cond_broadcast
 *  @status done
 */

/* Includes */
#include <syscall.h>
#include <stdlib.h>
#include <thread.h>
#include <mutex.h>
#include <cond.h>
#include "410_tests.h"
#include <report.h>
#include <test.h>

DEF_TEST_NAME("broadcast-test:");

#define STACK_SIZE 4096

#define ERR REPORT_FAILOUT_ON_ERR

mutex_t lock;
cond_t cvar;
static int ready = 0;
static int num_woken = 0;

void *waiter(void *dummy)
{
	
	mutex_lock(&lock);
	while (ready == 0) {
		cond_wait(&cvar, &lock);
	}
	num_woken++;
	if (num_woken == 2) {
		report_end(END_SUCCESS);
	}
	mutex_unlock(&lock);
	return NULL;
}
	

int main(void)
{
	report_start(START_CMPLT);

	ERR(thr_init(STACK_SIZE));
	ERR(mutex_init(&lock));
	ERR(cond_init(&cvar));
	misbehave(BGND_BRWN >> FGND_CYAN); // for landslide

	ERR(thr_create(waiter, NULL));
	//ERR(thr_create(waiter, NULL));

	mutex_lock(&lock);
	ready = 1;
	cond_broadcast(&cvar);
	mutex_unlock(&lock);

	return 0;
}
