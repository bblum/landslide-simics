/**
 * @file messaging.c
 * @brief routines for communicating with the iterative deepening wrapper
 * @author Ben Blum
 */

#define MODULE_NAME "MESSAGING"

#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "compiler.h"
#include "estimate.h"
#include "messaging.h"
#include "student_specifics.h"
#include "stack.h"

/* Spec. */

#define MESSAGE_BUF_SIZE 256

struct output_message {
	unsigned int magic;

	enum {
		THUNDERBIRDS_ARE_GO = 0,
		DATA_RACE = 1,
		ESTIMATE = 2,
		FOUND_A_BUG = 3,
		SHOULD_CONTINUE = 4,
		ASSERT_FAILED = 5,
	} tag;

	union {
		struct {
			unsigned int eip;
			unsigned int tid;
			unsigned int last_call;
			unsigned int most_recent_syscall;
			bool confirmed;
			char pretty_printed[MESSAGE_BUF_SIZE];
		} dr;

		struct {
			long double proportion;
			unsigned int elapsed_branches;
			long double total_usecs;
			long double elapsed_usecs;
			unsigned int icb_cur_bound;
		} estimate;

		struct {
			char trace_filename[MESSAGE_BUF_SIZE];
			unsigned int icb_preemption_count;
		} bug;

		struct {
			char assert_message[MESSAGE_BUF_SIZE];
		} crash_report;
	} content;
};

struct input_message {
	unsigned int magic;
	enum {
		SHOULD_CONTINUE_REPLY = 0,
		SUSPEND_TIME = 1,
		RESUME_TIME = 2,
	} tag;
	bool value;
};

/******************************************************************************
 * glue
 ******************************************************************************/

#ifdef ID_WRAPPER_MAGIC

static void send(struct messaging_state *state, struct output_message *m)
{
	assert(state->pipes_opened);
	m->magic = ID_WRAPPER_MAGIC;
	int ret = write(state->output_fd, m, sizeof(struct output_message));
	assert(ret == sizeof(struct output_message) && "write failed");
}

static void recv(struct messaging_state *state, struct input_message *m)
{
	assert(state->pipes_opened);
	int ret = read(state->input_fd, m, sizeof(struct input_message));
	if (ret == 0) {
		/* pipe closed */
		m->tag = SHOULD_CONTINUE_REPLY;
		m->value = true;
	} else if (ret != sizeof(struct input_message)) {
		assert(false && "read failed");
	} else {
		assert(m->magic == ID_WRAPPER_MAGIC && "wrong magic");
	}
}

#else /* !defined ID_WRAPPER_MAGIC */

static void send(struct messaging_state *state, struct output_message *m) { }

static void recv(struct messaging_state *state, struct input_message *m) {
	m->tag = SHOULD_CONTINUE_REPLY;
	m->value = false;
}

#endif

/******************************************************************************
 * messaging logic
 ******************************************************************************/

void messaging_init(struct messaging_state *state)
{
	state->pipes_opened = false;
}

void messaging_open_pipes(struct messaging_state *state,
			  const char *input_name, const char *output_name)
{
#ifdef ID_WRAPPER_MAGIC
	assert(!state->pipes_opened && "double call of messaging open pipes");
	state->pipes_opened = true;

	assert(input_name != NULL && output_name != NULL &&
	       "have magic quicksand cookie but how do i get to warp zone?");

	/* See run_job() in id/job.c for the protocol. Order is important. */
	lsprintf(INFO, "opening output pipe %s\n", output_name);
	state->output_fd = open(output_name, O_WRONLY);
	lsprintf(INFO, "the hatches are open\n");
	assert(state->output_fd >= 0 && "opening output pipe failed");

	struct output_message m;
	m.tag = THUNDERBIRDS_ARE_GO;
	send(state, &m);

	lsprintf(INFO, "opening input pipe %s\n", input_name);
	state->input_fd = open(input_name, O_RDONLY);
	lsprintf(INFO, "aim for the open spot\n");
	assert(state->input_fd >= 0 && "opening input pipe failed");
#else
	assert(input_name == NULL && output_name == NULL &&
	       "can't use messaging pipes without the magic quicksand cookie!");
#endif
}

void message_data_race(struct messaging_state *state, unsigned int eip,
		       unsigned int tid, unsigned int last_call,
		       unsigned int most_recent_syscall, bool confirmed)
{
	struct output_message m;
	m.tag = DATA_RACE;
	m.content.dr.eip = eip;
#ifdef FILTER_DRS_BY_TID
	m.content.dr.tid = tid;
#else
	m.content.dr.tid = DR_TID_WILDCARD;
#endif
	m.content.dr.last_call = last_call;
	m.content.dr.most_recent_syscall = most_recent_syscall;
	m.content.dr.confirmed = confirmed;

	char *buf = &m.content.dr.pretty_printed[0];
	struct stack_frame f;
	unsigned int pos = 0;

	eip_to_frame(eip, &f);
	pos += sprint_frame(buf + pos, MESSAGE_BUF_SIZE - pos, &f, false);
	destroy_frame(&f);

	if (last_call != 0) {
		eip_to_frame(last_call, &f);
		pos += scnprintf(buf + pos, MESSAGE_BUF_SIZE - pos, " [called @ ");
		pos += sprint_frame(buf + pos, MESSAGE_BUF_SIZE - pos, &f, false);
		pos += scnprintf(buf + pos, MESSAGE_BUF_SIZE - pos, "]");
		destroy_frame(&f);
	}

	send(state, &m);
}

uint64_t message_estimate(struct messaging_state *state, long double proportion,
			  unsigned int elapsed_branches, long double total_usecs,
			  unsigned long elapsed_usecs,
			  unsigned int icb_preemptions, unsigned int icb_bound)
{
	struct output_message m;
	m.tag = ESTIMATE;
	m.content.estimate.proportion = proportion;
	m.content.estimate.elapsed_branches = elapsed_branches;
	m.content.estimate.total_usecs = total_usecs;
	m.content.estimate.elapsed_usecs = elapsed_usecs;
	//m.content.estimate.icb_preemption_count = icb_preemptions; // not needed
	m.content.estimate.icb_cur_bound = icb_bound;
	send(state, &m);

	/* Ask whether or not our execution is being suspended. If so we must
	 * record the pause and resume times to not screw up ETA estimates. */
	uint64_t time_asleep = 0;
	struct input_message result;
	recv(state, &result);
	if (result.tag == SUSPEND_TIME) {
		if (result.value == true) {
			/* YOU ARE BOTH SUSPENDED. */
			struct timeval tv;
			update_time(&tv);
			lsprintf(DEV, "suspending time\n");
			recv(state, &result);
			assert(result.tag == RESUME_TIME ||
			       result.tag == SHOULD_CONTINUE_REPLY);
			time_asleep = update_time(&tv);
			lsprintf(DEV, "resuming time (time asleep: %" PRIu64 ")\n",
				 time_asleep);
		}
	} else {
		/* pipe closed (or running in standalone mode) */
		assert(result.tag == SHOULD_CONTINUE_REPLY);
	}

	return time_asleep;
}

void message_found_a_bug(struct messaging_state *state, const char *trace_filename,
			 unsigned int icb_preemptions, unsigned int icb_bound)
{
	struct output_message m;
	m.tag = FOUND_A_BUG;
	assert(strlen(trace_filename) < MESSAGE_BUF_SIZE && "name too long");
	strcpy(m.content.bug.trace_filename, trace_filename);
	m.content.bug.icb_preemption_count = icb_preemptions;
	//m.content.bug.icb_cur_bound = icb_bound; // not needed
	send(state, &m);
}

bool should_abort(struct messaging_state *state)
{
	struct output_message m;
	m.tag = SHOULD_CONTINUE;
	send(state, &m);

	struct input_message result;
	recv(state, &result);
	assert(result.tag == SHOULD_CONTINUE_REPLY);
	return result.value;
}

void message_assert_fail(struct messaging_state *state, const char *message,
			 const char *file, unsigned int line, const char *function)
{
	struct output_message m;
	m.tag = ASSERT_FAILED;
	scnprintf(m.content.crash_report.assert_message, MESSAGE_BUF_SIZE,
		  "%s:%u: %s(): %s", file, line, function, message);
	send(state, &m);
}
