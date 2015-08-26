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
#include "symtable.h"

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
		} estimate;

		struct {
			char trace_filename[MESSAGE_BUF_SIZE];
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
	m->magic = ID_WRAPPER_MAGIC;
	int ret = write(state->output_fd, m, sizeof(struct output_message));
	assert(ret == sizeof(struct output_message) && "write failed");
}

static void recv(struct messaging_state *state, struct input_message *m)
{
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
		       unsigned int tid, unsigned int last_call,
		       unsigned int most_recent_syscall, bool confirmed)
{
	struct output_message m;
	m.tag = DATA_RACE;
	m.content.dr.eip = eip;
	m.content.dr.tid = tid;
	m.content.dr.last_call = last_call;
	m.content.dr.most_recent_syscall = most_recent_syscall;
	m.content.dr.confirmed = confirmed;
	/* pretty print the data race addr to propagate to master program */
	char *func;
	char *file;
	int line;
	bool res = symtable_lookup(eip, &func, &file, &line);
	if (!res || func == NULL) {
		scnprintf(m.content.dr.pretty_printed, MESSAGE_BUF_SIZE,
			  "TID%d 0x%.8x <unknown>", tid, eip);
	} else if (file == NULL) {
		scnprintf(m.content.dr.pretty_printed, MESSAGE_BUF_SIZE,
			  "TID%d 0x%.8x in %s <source unknown>", tid, eip, func);
	} else {
		scnprintf(m.content.dr.pretty_printed, MESSAGE_BUF_SIZE,
			  "TID%d 0x%.8x in %s (%s:%d)", tid, eip, func, file, line);
	}
	if (res) {
		if (func != NULL) MM_FREE(func);
		if (file != NULL) MM_FREE(file);
	}
	send(state, &m);
}

uint64_t message_estimate(struct messaging_state *state, long double proportion,
			  unsigned int elapsed_branches, long double total_usecs,
			  unsigned long elapsed_usecs)
{
	struct output_message m;
	m.tag = ESTIMATE;
	m.content.estimate.proportion = proportion;
	m.content.estimate.elapsed_branches = elapsed_branches;
	m.content.estimate.total_usecs = total_usecs;
	m.content.estimate.elapsed_usecs = elapsed_usecs;
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
