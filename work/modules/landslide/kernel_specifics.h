/**
 * @file kernel_specifics.h
 * @brief Guest-implementation-specific things landslide needs to know.
 * @author Ben Blum
 */

#ifndef __LS_KERNEL_SPECIFICS_H
#define __LS_KERNEL_SPECIFICS_H

#include "landslide.h"

int get_current_tcb(ls_state_t *ls);
int get_current_tid(ls_state_t *ls);

bool agent_is_appearing(ls_state_t *ls);
int agent_appearing(ls_state_t *ls);
bool agent_is_disappearing(ls_state_t *ls);
int agent_disappearing(ls_state_t *ls);

#endif
