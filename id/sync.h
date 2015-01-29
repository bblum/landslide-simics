/**
 * @file sync.h
 * @brief pthread sync convenience wrapper macros
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_SYNC_H
#define __ID_SYNC_H

/* mxes */

#define MUTEX_INIT(mx) do {					\
		int __ret = pthread_mutex_init((mx), NULL);	\
		assert(__ret == 0 && "failed mx init");		\
	} while (0)

#define LOCK(mx) do {						\
		int __ret = pthread_mutex_lock(mx);		\
		assert(__ret == 0 && "failed mx lock");		\
	} while (0)

#define UNLOCK(mx) do {						\
		int __ret = pthread_mutex_unlock(mx);		\
		assert(__ret == 0 && "failed mx unlock");	\
	} while (0)

/* cvars */

#define COND_INIT(cond) do {					\
		int __ret = pthread_cond_init((cond), NULL);	\
		assert(__ret == 0 && "failed cond init");	\
	} while (0)

#define WAIT(cond, mx) do {					\
		int __ret = pthread_cond_wait((cond), (mx));	\
		assert(__ret == 0 && "failed wait");		\
	} while (0)

#define SIGNAL(cond) do {					\
		int __ret = pthread_cond_signal(cond);		\
		assert(__ret == 0 && "failed signal");		\
	} while (0)

#define BROADCAST(cond) do {					\
		int __ret = pthread_cond_broadcast(cond);	\
		assert(__ret == 0 && "failed broadcast");	\
	} while (0)

/* rwlox */

#define RWLOCK_INIT(rwlock) do {				\
		int __ret = pthread_rwlock_init((rwlock), NULL);\
		assert(__ret == 0 && "failed rwlock init");	\
	} while (0)

#define READ_LOCK(rwlock) do {					\
		int __ret = pthread_rwlock_rdlock(rwlock);	\
		assert(__ret == 0 && "failed rdlock");		\
	} while (0)

#define WRITE_LOCK(rwlock) do {					\
		int __ret = pthread_rwlock_wrlock(rwlock);	\
		assert(__ret == 0 && "failed wrlock");		\
	} while (0)

#define RW_UNLOCK(rwlock) do {					\
		int __ret = pthread_rwlock_unlock(rwlock);	\
		assert(__ret == 0 && "failed unlock");		\
	} while (0)

#endif
