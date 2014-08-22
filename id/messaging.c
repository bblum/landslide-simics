/**
 * @file messaging.c
 * @brief talking to child landslides
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "messaging.h"

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

bool wait_for_child(int input_fd)
{
	struct input_message m;
	if (recv(input_fd, &m)) {
		assert(m.tag == THUNDERBIRDS_ARE_GO && "wrong 1st message type");
		printf("recvd thunderbirds\n");
		return true;
	} else {
		/* child died before could send first message :( */
		printf("no thunderbirds\n");
		return false;
	}
}

void talk_to_child(int input_fd, int output_fd)
{
	struct input_message m;
	while (recv(input_fd, &m)) {
		if (m.tag == THUNDERBIRDS_ARE_GO) {
		} else if (m.tag == DATA_RACE) {
			printf("message DR @ 0x%x, MRS %x, %s\n",
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
			send(output_fd, &reply);
			printf("should continue? replied yes\n");
		} else {
			assert(false && "unknown message type");
		}
	}
}
