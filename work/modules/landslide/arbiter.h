/**
 * @file arbiter.h
 * @author Ben Blum
 * @brief decision-making routines for landslide
 */

#ifndef __LS_ARBITER_H
#define __LS_ARBITER_H

struct ls_state;
struct sched_state;
struct agent;

bool arbiter_interested(struct ls_state *);
struct agent *arbiter_choose(struct sched_state *);

#endif
