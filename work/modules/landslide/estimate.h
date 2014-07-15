/**
 * @file estimate.h
 * @brief online state space size estimation
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_ESTIMATE_H
#define __LS_ESTIMATE_H

#include <sys/time.h>

struct hax;
struct ls_state;

long double estimate_time(struct hax *root, struct hax *current);
long double estimate_proportion(struct hax *root, struct hax *current);
void print_estimates(struct ls_state *ls);

#endif
