/** @file cond.h
 *  @brief This file defines the interface for condition variables.
 */

#ifndef COND_H
#define COND_H

#include <mutex.h>
#include <cond_type.h>

/* condition variable functions */
int cond_init( cond_t *cv );
void cond_destroy( cond_t *cv );
void cond_wait( cond_t *cv, mutex_t *mp );
void cond_signal( cond_t *cv );
void cond_broadcast( cond_t *cv );

#endif /* COND_H */
