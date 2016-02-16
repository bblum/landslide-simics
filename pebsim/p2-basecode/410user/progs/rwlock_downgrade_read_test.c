/** 
 * @file rwlock_downgrade_read_test.c
 * @brief test readers vs downgrading writers for rwlock
 * @author Ben Blum <bblum> F2011
 */
#include <thread.h>
#include <mutex.h>
#include <cond.h>
#include <rwlock.h>
#include <syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include "410_tests.h"
#include <test.h>
DEF_TEST_NAME("rwlock_downgrade_read_test:");

#define NAME_OF_TEST "rwlock_downgrade_read_test"
#define STACK_SIZE 4096

int read_count = 0;
mutex_t read_count_lock;

rwlock_t lock;

int pass = -1;

void g()
{
	REPORT_MISC("Incrementing read_count");
	mutex_lock(&read_count_lock);
	read_count++;
	if (read_count == 2) {
		pass = 0;
	}
	mutex_unlock(&read_count_lock);
}

void *f(void *arg)
{
	REPORT_MISC("all mimsy were the reader threads");
	rwlock_lock(&lock, RWLOCK_READ);
	g();
	return((void *)0);
}

int main()
{
	int tid1;

	REPORT_LOCAL_INIT;
	REPORT_START_CMPLT;

	thr_init(STACK_SIZE);

	REPORT_ON_ERR(rwlock_init(&lock));
	REPORT_ON_ERR(mutex_init(&read_count_lock));
	rwlock_lock(&lock, RWLOCK_WRITE);

	if ((tid1 = thr_create(f, NULL)) < 0) {
		REPORT_MISC("Failed create");
		REPORT_END_FAIL;
		return -1;
	}
	rwlock_downgrade(&lock);
	REPORT_MISC("and the mome raths downgrade");

	g();
	
	/* "... and hast thou slain the jabberlock? come to my arms, my beamish
	 * boy!" */
	if (thr_join(tid1, NULL) < 0) {
		REPORT_MISC("Failed join");
		REPORT_END_FAIL;
		return -1;
	}

	if (pass == 0)
		REPORT_END_SUCCESS;
	else
		REPORT_END_FAIL;

	thr_exit((void *)pass);
	return pass;
}
