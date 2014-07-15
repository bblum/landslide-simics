/**
 * @file explore.h
 * @brief choice tree exploration
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_EXPLORE_H
#define __LS_EXPLORE_H

struct arbiter_state;
struct hax;
struct save_state;

struct hax *explore(struct save_state *ss, unsigned int *new_tid);

#endif
