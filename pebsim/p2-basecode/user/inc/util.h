#ifndef __UTIL_H_
#define __UTIL_H_

#include <stdlib.h>
#include <stdio.h>
#include <thread.h>
#include <cond.h>
#include <fake_io.h>

#define DEBUG_PRINT 0

#define dbg(fmt, args...) \
    do { if (DEBUG_PRINT) fprintf(stderr, "tid %d: "fmt, thr_getid(),  args); } while(0)

//#define panic(...) do {fprintf(stderr,"FATAL: "__VA_ARGS__); abort();} while(0)

int pthread_cond_select(cond_t* c1, mutex_t* m1,
                        cond_t* c2, mutex_t* m2);

#endif /* __UTIL_H_ */
