/** @file sem.h
 *  @brief This file defines the interface to semaphores
 */

#ifndef SEM_H
#define SEM_H

#include <sem_type.h>

/* semaphore functions */
int sem_init( sem_t *sem, int count );
void sem_wait( sem_t *sem );
void sem_signal( sem_t *sem );
void sem_destroy( sem_t *sem );

#endif /* SEM_H */
