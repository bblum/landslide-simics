/**
 * @file messaging.c
 * @brief talking to child landslides
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "messaging.h"
#include "time.h"
#include "xcalls.h"

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

/* glue */

static void send(int output_fd, struct output_message *m)
{
	m->magic = MESSAGING_MAGIC;
	int ret = write(output_fd, m, sizeof(struct output_message));
	assert(ret == sizeof(struct output_message) &&
	       "write output msg failed");
}

static bool recv(int input_fd, struct input_message *m)
{
	int ret = read(input_fd, m, sizeof(struct input_message));
	if (ret == sizeof(struct input_message)) {
		assert(m->magic == MESSAGING_MAGIC && "wrong magic");
		return true;
	} else {
		/* pipe was closed before next message was sent */
		assert(ret == 0 && "read input msg failed");
		return false;
	}
}

/* logic */

/* creates the fifo files on the filesystem, but does not block on them yet. */
void messaging_init(struct messaging_state *state, struct file *config_file,
		    unsigned int job_id)
{
	state->input_pipe_name  = create_fifo("id-input-pipe",  job_id);
	state->output_pipe_name = create_fifo("id-output-pipe", job_id);
	state->ready = false;

	/* our output is the child's input and V. V. */
	XWRITE(config_file, "output_pipe %s\n", state->input_pipe_name);
	XWRITE(config_file, "input_pipe %s\n", state->output_pipe_name);
	XWRITE(config_file, "id_magic %u\n", MESSAGING_MAGIC);
}

bool wait_for_child(struct messaging_state *state)
{
	assert(state->input_pipe_name  != NULL);
	assert(state->output_pipe_name != NULL);
	assert(!state->ready);

	// TODO: use ansi escape codes for 'invisible' messages to help debug
	// XXX: using fifos instead of anonymous pipes, impossible to tell
	// whether e.g. the build failed. maybe use a timed wait?
	open_fifo(&state->input_pipe, state->input_pipe_name, O_RDONLY);
	state->input_pipe_name = NULL;

	struct input_message m;
	if (recv(state->input_pipe.fd, &m)) {
		assert(m.tag == THUNDERBIRDS_ARE_GO && "wrong 1st message type");
		/* child is alive. finalize the 2-way fifo setup. */
		open_fifo(&state->output_pipe, state->output_pipe_name, O_WRONLY);
		state->output_pipe_name = NULL;
		state->ready = true;
		return true;
	} else {
		/* child died before could send first message :( */
		return false;
	}
}

void talk_to_child(struct messaging_state *state)
{
	assert(state->ready);

	struct input_message m;
	while (recv(state->input_pipe.fd, &m)) {
		if (m.tag == THUNDERBIRDS_ARE_GO) {
		} else if (m.tag == DATA_RACE) {
			printf("message DR @ 0x%x, MRS 0x%x, %s\n",
			       m.content.dr.eip, m.content.dr.most_recent_syscall,
			       m.content.dr.confirmed ? "confirmed" : "suspected");
		} else if (m.tag == ESTIMATE) {
			printf("message est: %Lf%% of %u brs, %Lf elapsed / %Lf total\n",
			       m.content.estimate.proportion * 100,
			       m.content.estimate.estimated_branches,
			       m.content.estimate.elapsed_usecs,
			       m.content.estimate.total_usecs);
		} else if (m.tag == FOUND_A_BUG) {
			printf("message FAB, fname %s\n", m.content.bug.trace_filename);
		} else if (m.tag == SHOULD_CONTINUE) {
			// TODO
			struct output_message reply;
			reply.do_abort = false;
			send(state->output_pipe.fd, &reply);
			printf("should continue? replied yes; time left %lu\n",
			       time_remaining());
		} else {
			assert(false && "unknown message type");
		}
	}
}

void finish_messaging(struct messaging_state *state)
{
	delete_file(&state->input_pipe, true);
	delete_file(&state->output_pipe, true);
}
