/**
 * @file explore.h
 * @brief choice tree exploration
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#ifndef __LS_EXPLORE_H
#define __LS_EXPLORE_H

struct arbiter_state;
struct hax;

struct hax *explore(struct hax *root, struct hax *current, int *new_tid);

#endif
