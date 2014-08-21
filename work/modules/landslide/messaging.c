/**
 * @file messaging.c
 * @brief routines for communicating with the iterative deepening wrapper
 * @author Ben Blum
 */

#define MODULE_NAME "MESSAGING"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "compiler.h"
#include "messaging.h"
#include "student_specifics.h"

/* Spec. */

#define MESSAGE_BUF_SIZE 128

struct output_message {
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

struct input_message {
	unsigned int magic;
	bool do_abort;
};

/******************************************************************************
 * glue
 ******************************************************************************/

#ifdef ID_WRAPPER_MAGIC

static void send(struct messaging_state *state, struct output_message *m)
{
	m->magic = ID_WRAPPER_MAGIC;
	int ret = write(state->output_fd, m, sizeof(struct output_message));
	assert(ret == sizeof(struct output_message) && "write failed");
}

static void recv(struct messaging_state *state, struct input_message *m)
{
	int ret = read(state->input_fd, m, sizeof(struct input_message));
	if (ret == 0) {
		/* pipe closed */
		m->do_abort = true;
	} else if (ret != sizeof(struct input_message)) {
		assert(false && "read failed");
	} else {
		assert(m->magic == ID_WRAPPER_MAGIC && "wrong magic");
	}
}

#else /* !defined ID_WRAPPER_MAGIC */

static void send(struct messaging_state *state, struct output_message *m) { }

static void recv(struct messaging_state *state, struct input_message *m) {
	m->do_abort = false;
}

#endif

/******************************************************************************
 * messaging logic
 ******************************************************************************/

void messaging_init(struct messaging_state *state)
{
#ifdef ID_WRAPPER_MAGIC
	/* See run_job() in id/job.c for the protocol. Order is important. */
#ifdef OUTPUT_PIPE
	lsprintf(INFO, "opening output pipe %s\n", OUTPUT_PIPE);
	state->output_fd = open(OUTPUT_PIPE, O_WRONLY);
	lsprintf(INFO, "the hatches are open\n");
	assert(state->output_fd >= 0 && "opening output pipe failed");
#else
	STATIC_ASSERT(false && "ID magic but OUTPUT_PIPE not defined");
#endif

	struct output_message m;
	m.tag = THUNDERBIRDS_ARE_GO;
	send(state, &m);

#ifdef INPUT_PIPE
	lsprintf(INFO, "opening input pipe %s\n", INPUT_PIPE);
	state->input_fd = open(INPUT_PIPE, O_RDONLY);
	lsprintf(INFO, "aim for the open spot\n");
	assert(state->input_fd >= 0 && "opening input pipe failed");
#else
	STATIC_ASSERT(false && "ID magic but INPUT_PIPE not defined");
#endif
#else
	/* Not running in ID wrapper. Nothing to initialize. */
#endif
}

void message_data_race(struct messaging_state *state, unsigned int eip,
		       unsigned int most_recent_syscall, bool confirmed)
{
	struct output_message m;
	m.tag = DATA_RACE;
	m.content.dr.eip = eip;
	m.content.dr.most_recent_syscall = most_recent_syscall;
	m.content.dr.confirmed = confirmed;
	send(state, &m);
}

void message_estimate(struct messaging_state *state, long double proportion,
		      unsigned int estimated_branches, long double total_usecs,
		      unsigned long elapsed_usecs)
{
	struct output_message m;
	m.tag = ESTIMATE;
	m.content.estimate.proportion = proportion;
	m.content.estimate.estimated_branches = estimated_branches;
	m.content.estimate.total_usecs = total_usecs;
	m.content.estimate.elapsed_usecs = elapsed_usecs;
	send(state, &m);
}

void message_found_a_bug(struct messaging_state *state, const char *trace_filename)
{
	struct output_message m;
	m.tag = FOUND_A_BUG;
	assert(strlen(trace_filename) < MESSAGE_BUF_SIZE && "name too long");
	strcpy(m.content.bug.trace_filename, trace_filename);
	send(state, &m);
}

bool should_abort(struct messaging_state *state)
{
	struct output_message m;
	m.tag = SHOULD_CONTINUE;
	send(state, &m);

	struct input_message result;
	recv(state, &result);
	return result.do_abort;
}
