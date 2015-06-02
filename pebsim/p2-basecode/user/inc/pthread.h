#ifndef __PTHREAD_H__
#define __PTHREAD_H__

#include <thread.h>
#include <mutex.h>
#include <cond.h>

typedef int pthread_t;
typedef mutex_t pthread_mutex_t;
typedef cond_t pthread_cond_t;

#define pthread_create(tidp, opts, body, args)	\
	do { *(tidp) = thr_create(body, args); } while  (0)

#define pthread_join(tid, status) thr_join(tid, status)
#define pthread_exit(arg)         thr_exit(arg)
#define pthread_detach(arg)       thr_detach(arg)

#define pthread_mutex_init(m, x)  mutex_init(m)
#define pthread_mutex_destroy(m)  mutex_destroy(m)
#define pthread_mutex_lock(m)     mutex_lock(m)
#define pthread_mutex_unlock(m)   mutex_unlock(m)

#define pthread_cond_init(c, x)   cond_init(c)
#define pthread_cond_destroy(c)   cond_destroy(c)
#define pthread_cond_wait(c, m)   cond_wait(c, m)
#define pthread_cond_signal(c)    cond_signal(c)
#define pthread_cond_broadcast(c) cond_broadcast(c)

#endif
