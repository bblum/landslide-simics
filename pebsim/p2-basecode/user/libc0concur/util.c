//#include <time.h>

#include <util.h>

/* const struct timespec cond_delay = {.tv_sec = 0, .tv_nsec = 100}; */

/* int pthread_cond_select(pthread_cond_t* c1, pthread_mutex_t* m1, */
/*                         pthread_cond_t* c2, pthread_mutex_t* m2) */
/* { */
/*     int wait_result = 0; */
/*     while (1) { */
/*         pthread_mutex_unlock(m2); */
/*         wait_result = pthread_cond_timedwait(c1, m1, cond_delay); */
/*         pthread_mutex_lock(m2) */
/*         if (wait_result != ETIMEDOUT) return wait_result; */

/*         pthread_mutex_unlock(m1); */
/*         wait_result = pthread_cond_timedwait(c2, m2, cond_delay); */
/*         pthread_mutex_lock(m1) */
/*         if (wait_result != ETIMEDOUT) return wait_result; */
/*     } */
/* } */
