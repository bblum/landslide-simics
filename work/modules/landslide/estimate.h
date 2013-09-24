/**
 * @file estimate.h
 * @brief online state space size estimation
 * @author Ben Blum <bbum@andrew.cmu.edu>
 */

#ifndef __LS_ESTIMATE_H
#define __LS_ESTIMATE_H

struct hax;

void estimate(struct hax *root, struct hax *current);

#endif
