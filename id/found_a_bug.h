/**
 * @file found_a_bug.h
 * @brief remembering which bugs have been found under which pp configs
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_FOUND_A_BUG_H
#define __ID_FOUND_A_BUG_H

#include <stdbool.h>

void found_a_bug(char *trace_filename);

bool found_any_bugs();

#endif
