/**
 * @file messaging.h
 * @brief routines for communicating with the iterative deepening wrapper
 * @author Ben Blum
 */

#ifndef __LS_MESSAGING_H
#define __LS_MESSAGING_H

struct messaging_state {
	int input_fd;
	int output_fd;
};

void messaging_init(struct messaging_state *m);

#define DR_TID_WILDCARD 0x15410de0u /* 0 could be a valid tid */
void message_data_race(struct messaging_state *m, unsigned int eip,
		       unsigned int last_call, unsigned int tid,
		       unsigned int most_recent_syscall, bool confirmed);

/* returns the # of useconds that landslide was put to sleep for */
uint64_t message_estimate(struct messaging_state *m, long double proportion,
			  unsigned int elapsed_branches, long double total_usecs,
			  unsigned long elapsed_usecs);

void message_found_a_bug(struct messaging_state *m, const char *trace_filename);

bool should_abort(struct messaging_state *m);

void message_assert_fail(struct messaging_state *state, const char *message,
			 const char *file, unsigned int line, const char *function);

#endif
