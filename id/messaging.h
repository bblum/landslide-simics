/**
 * @file messaging.h
 * @brief talking to child landslides
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __ID_MESSAGING_H
#define __ID_MESSAGING_H

#include <stdbool.h>

#include "io.h"

struct messaging_state {
	char *input_pipe_name;
	char *output_pipe_name;
	struct file input_pipe;
	struct file output_pipe;
	bool ready;
};

void messaging_init(struct messaging_state *state, struct file *config_file,
		    unsigned int job_id);
bool wait_for_child(struct messaging_state *state);
void talk_to_child(struct messaging_state *state);
void finish_messaging(struct messaging_state *state);

#endif
