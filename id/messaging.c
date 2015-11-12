/**
 * @file messaging.c
 * @brief talking to child landslides
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bug.h"
#include "job.h"
#include "messaging.h"
#include "pp.h"
#include "sync.h"
#include "time.h"
#include "work.h"
#include "xcalls.h"

#define MESSAGING_MAGIC 0x15410de0u
#define DR_TID_WILDCARD 0x15410de0u /* 0 could be a valid tid */

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
			unsigned int tid;
			unsigned int last_call;
			unsigned int most_recent_syscall;
			bool confirmed;
			bool deterministic;
			bool free_re_malloc;
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

struct output_message {
	unsigned int magic;
	enum {
		SHOULD_CONTINUE_REPLY = 0,
		SUSPEND_TIME = 1,
		RESUME_TIME = 2,
	} tag;
	bool value;
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
extern bool use_icb;
extern bool verbose;

static void handle_data_race(struct job *j, struct pp_set **discovered_pps,
			     unsigned int eip, unsigned int tid, bool confirmed,
			     bool deterministic, bool free_re_malloc, unsigned int last_call,
			     unsigned int most_recent_syscall, char *pretty)
{
	/* register a (possibly) new PP based on the data race */
	bool duplicate;
	char config_str[BUF_SIZE];
	char short_str[BUF_SIZE];
	MAKE_DR_PP_STR(config_str, BUF_SIZE, eip, tid, last_call, most_recent_syscall);
	const char *dr_str = verbose ? "DR" : "data race";
	if (verbose && last_call != 0) {
		if (tid == DR_TID_WILDCARD) {
			scnprintf(short_str, BUF_SIZE, "%s @ 0x%x(0x%x)",
				  dr_str, eip, last_call);
		} else {
			scnprintf(short_str, BUF_SIZE, "%s %u@ 0x%x(0x%x)",
				  dr_str, tid, eip, last_call);
		}
	} else {
		if (tid == DR_TID_WILDCARD) {
			scnprintf(short_str, BUF_SIZE, "%s @ 0x%x", dr_str, eip);
		} else {
			scnprintf(short_str, BUF_SIZE, "%s %u@ 0x%x",
				  dr_str, tid, eip);
		}
	}

	unsigned int priority = confirmed ?
		PRIORITY_DR_CONFIRMED : PRIORITY_DR_SUSPECTED;
	struct pp *pp = pp_new(config_str, short_str, pretty, priority,
			       deterministic, free_re_malloc, j->generation, &duplicate);
	// Uncomment this to make LS/QS more comparable to 1-pass DR.
	// Free-re-malloc PPs will be recorded as deterministic, so we don't
	// unfairly classify them as false negatives.
//#define DR_FALSE_NEGATIVE_EXPERIMENT
#ifdef DR_FALSE_NEGATIVE_EXPERIMENT
	// Cripple-myself mode. Add the free remalloc DR as a PP (so long as it
	// was discovered on the 0th branch).
	// (But don't cripple myself TOO much, if it would be a nondet DR PP anyway)
	if (free_re_malloc && !deterministic) return;
#else
	// Normal mode. Never add the free remalloc DR as a PP. (At least until
	// the same DR is observed in future branch w/o free-remalloc pattern.)
	if (free_re_malloc) return;
#endif

	/* If the data race PP is not already enabled in this job's config,
	 * create a new job based on this one. */
	if (j->should_reproduce && !pp_set_contains(j->config, pp) &&
	    !pp_set_contains(*discovered_pps, pp) && !control_experiment &&
	    !bug_already_found(j->config)) {
		struct pp_set *new_set;
		bool added = false;
		/* Add a little job. */
		if (!duplicate && j->config->size > 0) {
			struct pp_set *empty = create_pp_set(PRIORITY_NONE);
			new_set = add_pp_to_set(empty, pp);
			free_pp_set(empty);
			if (work_already_exists(new_set)) {
				free_pp_set(new_set);
			} else {
				DBG("Adding small job with new PP '%s'\n",
				    pp->config_str);
				add_work(new_job(new_set, false));
				added = true;
			}
		}
		/* Add a big job. */
		new_set = add_pp_to_set(j->config, pp);
		if (work_already_exists(new_set) || bug_already_found(new_set)) {
			free_pp_set(new_set);
		} else {
			DBG("Adding big job with new PP '%s'\n", pp->config_str);
			add_work(new_job(new_set, true));
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

extern unsigned long eta_factor;
extern unsigned long eta_threshold;

/* Given the 30sec or so overhead in compiling and setting up a new state space,
 * once we get close enough to the end it's not worth trying to context switch
 * to fresh jobs. */
#define HOMESTRETCH (60 * 1000000)

static void handle_estimate(struct messaging_state *state, struct job *j,
			    long double proportion, unsigned int elapsed_branches,
			    long double total_usecs, long double elapsed_usecs,
			    unsigned int icb_bound)
{
	unsigned int total_branches =
	    (unsigned int)((long double)elapsed_branches / proportion);
	long double remaining_usecs = total_usecs - elapsed_usecs;

	WRITE_LOCK(&j->stats_lock);
	j->elapsed_branches = elapsed_branches;
	j->estimate_proportion = proportion;
	human_friendly_time(elapsed_usecs, &j->estimate_elapsed);
	j->estimate_eta_numeric = remaining_usecs;
	human_friendly_time(remaining_usecs, &j->estimate_eta);
	DBG("[JOB %d] progress: %u/%u brs (%Lf%%), ", j->id,
	    elapsed_branches, total_branches, proportion * 100);
	if (use_icb) {
		DBG("ICB @ %u, ", icb_bound);
		j->icb_current_bound = icb_bound;
	}
	DBG("ETA ");
	dbg_human_friendly_time(&j->estimate_eta);
	DBG(" (elapsed ");
	dbg_human_friendly_time(&j->estimate_elapsed);
	DBG(")\n");
	RW_UNLOCK(&j->stats_lock);

	/* Does this ETA suck? (note all numbers here are in usecs) */
	bool eta_overflow = remaining_usecs > (long double)ULONG_MAX;
	unsigned long eta = (unsigned long)remaining_usecs;
	unsigned long time_left = time_remaining();

	struct output_message reply;
	reply.tag = SUSPEND_TIME;

	assert(eta_factor >= 1);
	if (elapsed_branches >= eta_threshold && time_left > HOMESTRETCH &&
	    (eta_overflow || time_left * eta_factor < eta) &&
	    should_work_block(j)) {
		WARN("[JOB %d] State space too big (%u brs elapsed, "
		     "time rem %lu, eta %lu) -- blocking!\n", j->id,
		     elapsed_branches, time_left / 1000000, eta / 1000000);
		/* Inform landslide instance to pause its time counter. */
		reply.value = true;
		send(state->output_pipe.fd, &reply);
		/* Wait until we get rescheduled. */
		job_block(j);
		/* Tell landslide instance to start timing again. */
		reply.tag = RESUME_TIME;
		send(state->output_pipe.fd, &reply);
	} else {
		/* Normal operation. Tell landslide not to pause timing. */
		reply.value = false;
		send(state->output_pipe.fd, &reply);
	}
}

static bool handle_should_continue(struct job *j)
{
	if (bug_already_found(j->config)) {
		DBG("Aborting -- a subset of our PPs already found a bug.\n");
		WRITE_LOCK(&j->stats_lock);
		j->cancelled = true;
		RW_UNLOCK(&j->stats_lock);
		return false;
	} else if (TIME_UP()) {
		DBG("Aborting -- time up!\n");
		WRITE_LOCK(&j->stats_lock);
		j->timed_out = true;
		RW_UNLOCK(&j->stats_lock);
		return false;
	} else {
		READ_LOCK(&j->stats_lock);
		bool should_kill_job = j->kill_job;
		RW_UNLOCK(&j->stats_lock);
		if (should_kill_job) {
			DBG("Aborting -- can't swap!\n");
			WRITE_LOCK(&j->stats_lock);
			j->cancelled = true;
			RW_UNLOCK(&j->stats_lock);
			return false;
		} else {
			return true;
		}
	}
}

static void handle_crash(struct job *j, struct input_message *m)
{
	WRITE_LOCK(&j->stats_lock);
	j->cancelled = true;
	RW_UNLOCK(&j->stats_lock);

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
				ERR("[JOB %d] However, you may wish to manually "
				    "inspect the following data race(s):\n", j->id);
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
void messaging_init(struct messaging_state *state, struct file *config_static,
		    struct file *config_dynamic, unsigned int job_id)
{
	state->input_pipe_name  = create_fifo("id-input-pipe",  job_id);
	state->output_pipe_name = create_fifo("id-output-pipe", job_id);
	state->ready = false;

	/* our output is the child's input and V. V. */
	XWRITE(config_dynamic, "output_pipe %s\n", state->input_pipe_name);
	XWRITE(config_dynamic, "input_pipe %s\n", state->output_pipe_name);
	XWRITE(config_static, "id_magic %u\n", MESSAGING_MAGIC);
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
					 m.content.dr.tid, m.content.dr.confirmed,
					 m.content.dr.deterministic,
					 m.content.dr.free_re_malloc,
					 m.content.dr.last_call,
					 m.content.dr.most_recent_syscall,
					 m.content.dr.pretty_printed);
		} else if (m.tag == ESTIMATE) {
			handle_estimate(state, j, m.content.estimate.proportion,
					m.content.estimate.elapsed_branches,
					m.content.estimate.total_usecs,
					m.content.estimate.elapsed_usecs,
					m.content.estimate.icb_cur_bound);
		} else if (m.tag == FOUND_A_BUG) {
			move_trace_file(m.content.bug.trace_filename);
			// NB. Harmless if/then/else race; could cause simply
			// extraneous bug reports when this races itself.
			if (bug_already_found(j->config)) {
				DBG("Ignoring bug report -- a subset of our "
				    "PPs already found a bug.\n");
				WRITE_LOCK(&j->stats_lock);
				j->cancelled = true;
				RW_UNLOCK(&j->stats_lock);
			} else {
				READ_LOCK(&j->stats_lock);
				/* this should scare you. it scares me. */
				bool need_rerun = testing_pintos() &&
					j->elapsed_branches == 0;
				RW_UNLOCK(&j->stats_lock);
				if (need_rerun) {
					WRITE_LOCK(&j->stats_lock);
					j->elapsed_branches++;
					j->need_rerun = true;
					RW_UNLOCK(&j->stats_lock);
				} else {
					/* actual logic */
					found_a_bug(m.content.bug.trace_filename, j);

					WRITE_LOCK(&j->stats_lock);
					assert(j->trace_filename == NULL &&
					       "bug already found same job?");
					j->trace_filename =
						XSTRDUP(m.content.bug.trace_filename);
					j->fab_timestamp = time_elapsed();
					j->fab_cputime = total_cpu_time();
					j->elapsed_branches++;
					j->icb_fab_preemptions =
						m.content.bug.icb_preemption_count;
					RW_UNLOCK(&j->stats_lock);
				}
			}
		} else if (m.tag == SHOULD_CONTINUE) {
			struct output_message reply;
			reply.tag = SHOULD_CONTINUE_REPLY;
			reply.value = !handle_should_continue(j);
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

void messaging_abort(struct messaging_state *state)
{
	assert(state->input_pipe_name != NULL);
	assert(state->output_pipe_name != NULL);
	delete_unused_fifo(state->input_pipe_name);
	delete_unused_fifo(state->output_pipe_name);
}
