/**
 * @file messaging.c
 * @brief talking to child landslides
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "bug.h"
#include "job.h"
#include "messaging.h"
#include "pp.h"
#include "time.h"
#include "work.h"
#include "xcalls.h"

#define MESSAGING_MAGIC 0x15410de0u

#define MESSAGE_BUF_SIZE 256

struct input_message {
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
			unsigned int most_recent_syscall;
			bool confirmed;
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

/* event handling logic */

extern bool control_experiment;

static void handle_data_race(struct job *j, struct pp_set **discovered_pps,
			     unsigned int eip, bool confirmed,
			     unsigned int most_recent_syscall)
{
	/* register a (possibly) new PP based on the data race */
	bool duplicate;
	char config_str[BUF_SIZE];
	MAKE_DR_PP_STR(config_str, BUF_SIZE, eip, most_recent_syscall);

	unsigned int priority = confirmed ?
		PRIORITY_DR_CONFIRMED : PRIORITY_DR_SUSPECTED;
	struct pp *pp = pp_new(config_str, priority, j->generation, &duplicate);

	/* If the data race PP is not already enabled in this job's config,
	 * create a new job based on this one. */
	if (j->should_reproduce && !pp_set_contains(j->config, pp) &&
	    !pp_set_contains(*discovered_pps, pp)) {
		DBG("Adding job with new PP '%s'\n", pp->config_str);
		struct pp_set *new_set;
		bool added = false;
		/* Add a little job. */
		if (!duplicate && j->config->size > 0) {
			struct pp_set *empty = create_pp_set(PRIORITY_NONE);
			new_set = add_pp_to_set(empty, pp);
			free_pp_set(empty);
			if (!control_experiment) {
				add_work(new_job(new_set, false));
			}
			added = true;
		}
		/* Add a big job. */
		new_set = add_pp_to_set(j->config, pp);
		if (bug_already_found(new_set)) {
			free_pp_set(new_set);
		} else {
			if (!control_experiment) {
				add_work(new_job(new_set, true));
			}
			added = true;
		}
		if (added) {
			signal_work();
		}
	}

	/* Record this in the set of PPs that were "discovered" by this job
	 * (ignoring discoveries by other threads). This allows us to decide
	 * when to create a new job -- the data race was not part of this job's
	 * initial config, nor did we already create the same job already. */
	struct pp_set *old_discovered = *discovered_pps;
	*discovered_pps = add_pp_to_set(old_discovered, pp);
	free_pp_set(old_discovered);
}

static void handle_estimate(struct job *j, long double proportion,
			    unsigned int elapsed_branches,
			    long double total_usecs, long double elapsed_usecs)
{
	unsigned int total_branches =
	    (unsigned int)((long double)elapsed_branches / proportion);
	struct human_friendly_time hft_elapsed;
	struct human_friendly_time hft_eta;
	human_friendly_time(elapsed_usecs, &hft_elapsed);
	human_friendly_time(total_usecs - elapsed_usecs, &hft_eta);
	DBG("[JOB %d] progress: %u/%u brs (%Lf%%), ETA ", j->id,
	    elapsed_branches, total_branches, proportion * 100);
	print_human_friendly_time(&hft_eta);
	DBG(" (elapsed ");
	print_human_friendly_time(&hft_elapsed);
	DBG(")\n");
	// TODO: update job state
	(void)j;
}

static bool handle_should_continue(struct job *j)
{
	if (bug_already_found(j->config)) {
		DBG("Aborting -- a subset of our PPs already found a bug.\n");
		return false;
	} else if (TIME_UP()) {
		DBG("Aborting -- time up!\n");
		return false;
	} else {
		return true;
	}
}

static void handle_crash(struct job *j, struct input_message *m)
{
	ERR("[JOB %d] Landslide crashed. The assert message was: %s\n",
	    j->id, m->content.crash_report.assert_message);
	ERR("[JOB %d] For more detail see stderr log file: %s\n",
	    j->id, j->log_stderr.filename);

	ERR("[JOB %d] THIS IS NOT YOUR FAULT.\n", j->id);
	struct pp *pp;
	bool any_drs = false;
	FOR_EACH_PP(pp, j->config) {
		if (pp->priority == PRIORITY_DR_SUSPECTED ||
		    pp->priority == PRIORITY_DR_CONFIRMED) {
			if (!any_drs) {
				any_drs = true;
				ERR("[JOB %d] However, please manually inspect "
				    "the following data race(s):\n", j->id);
			}
			ERR("[JOB %d] %s\n", j->id, pp->config_str);
		}
	}
}

static void move_trace_file(const char *trace_filename)
{
	/* + 2 because 1 for the '/' in between and 1 for the null. */
	unsigned int length_old =
		strlen(LANDSLIDE_PATH) + strlen(trace_filename) + 2;
	unsigned int length_new =
		strlen(ROOT_PATH) + strlen(trace_filename) + 2;
	char *old_path = XMALLOC(length_old, char);
	char *new_path = XMALLOC(length_new, char);
	scnprintf(old_path, length_old, "%s/%s", LANDSLIDE_PATH, trace_filename);
	scnprintf(new_path, length_new, "%s/%s", ROOT_PATH, trace_filename);
	XRENAME(old_path, new_path);
	FREE(old_path);
	FREE(new_path);
}

/* messaging logic */

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

void talk_to_child(struct messaging_state *state, struct job *j)
{
	assert(state->ready);
	struct pp_set *discovered_pps = create_pp_set(PRIORITY_NONE);

	struct input_message m;
	while (recv(state->input_pipe.fd, &m)) {
		if (m.tag == THUNDERBIRDS_ARE_GO) {
			assert(false && "recvd duplicate thunderbirds message");
		} else if (m.tag == DATA_RACE) {
			handle_data_race(j, &discovered_pps, m.content.dr.eip,
					 m.content.dr.confirmed,
					 m.content.dr.most_recent_syscall);
		} else if (m.tag == ESTIMATE) {
			handle_estimate(j, m.content.estimate.proportion,
					m.content.estimate.elapsed_branches,
					m.content.estimate.total_usecs,
					m.content.estimate.elapsed_usecs);
		} else if (m.tag == FOUND_A_BUG) {
			move_trace_file(m.content.bug.trace_filename);
			found_a_bug(m.content.bug.trace_filename, j);
		} else if (m.tag == SHOULD_CONTINUE) {
			struct output_message reply;
			reply.do_abort = !handle_should_continue(j);
			send(state->output_pipe.fd, &reply);
		} else if (m.tag == ASSERT_FAILED) {
			handle_crash(j, &m);
			break;
		} else {
			assert(false && "unknown message type");
		}
	}

	free_pp_set(discovered_pps);
}

void finish_messaging(struct messaging_state *state)
{
	assert(state->input_pipe_name == NULL);
	delete_file(&state->input_pipe, true);
	if (state->output_pipe_name == NULL) {
		delete_file(&state->output_pipe, true);
	} else {
		delete_unused_fifo(state->output_pipe_name);
	}
}
