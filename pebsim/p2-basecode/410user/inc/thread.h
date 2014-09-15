/** @file thread.h
 *  @brief This file defines thread-management interface.
 *
 *  It should NOT contain any of the thread library internals.
 *  Therefore, you may NOT modify this file.
 *
 *  However, you MAY modify user/libthread/thr_internals.h.
 *
 */



#ifndef THREAD_H
#define THREAD_H

#include <thr_internals.h>

/* thread library functions */
int thr_init( unsigned int size );
int thr_create( void *(*func)(void *), void *args );
int thr_join( int tid, void **statusp );
void thr_exit( void *status );
int thr_getid( void );
int thr_yield( int tid );

#endif /* THREAD_H */
