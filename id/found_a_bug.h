/**
 * @file found_a_bug.h
 * @brief remembering which bugs have been found under which pp configs
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_FOUND_A_BUG_H
#define __ID_FOUND_A_BUG_H

#include <stdbool.h>

struct pp_set;

void found_a_bug(char *trace_filename, struct pp_set *config);
bool bug_already_found(struct pp_set *config);
bool found_any_bugs();

#endif
