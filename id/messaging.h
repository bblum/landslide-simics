/**
 * @file messaging.h
 * @brief talking to child landslides
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_MESSAGING_H
#define __ID_MESSAGING_H

#include <stdbool.h>

#define MESSAGING_MAGIC 0x15410de0u

#define MESSAGE_BUF_SIZE 128

struct input_message {
	unsigned int magic;

	enum {
		THUNDERBIRDS_ARE_GO = 0,
		DATA_RACE = 1,
		ESTIMATE = 2,
		FOUND_A_BUG = 3,
		SHOULD_CONTINUE = 4,
	} tag;

	union {
		struct {
			unsigned int eip;
			unsigned int most_recent_syscall;
			bool confirmed;
		} dr;

		struct {
			long double proportion;
			unsigned int estimated_branches;
			long double total_usecs;
			long double elapsed_usecs;
		} estimate;

		struct {
			char trace_filename[MESSAGE_BUF_SIZE];
		} bug;
	} content;
};

struct output_message {
	unsigned int magic;
	bool do_abort;
};

bool wait_for_child(int input_fd);
void talk_to_child(int input_fd, int output_fd);

#endif
