/**
 * @file job.c
 * @brief job management
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "bug.h"
#include "common.h"
#include "job.h"
#include "io.h"
#include "messaging.h"
#include "pp.h"
#include "sync.h"
#include "time.h"
#include "xcalls.h"

static unsigned int job_id = 0;

static pthread_mutex_t compile_landslide_lock = PTHREAD_MUTEX_INITIALIZER;

extern char **environ;

// TODO-FIXME: Insert timestamps so log files are sorted chronologically.
#define CONFIG_STATIC_TEMPLATE  "config.quicksand.XXXXXX"
#define CONFIG_DYNAMIC_TEMPLATE "pps-and-such.quicksand.XXXXXX"
#define LOG_FILE_TEMPLATE(x) "ls-" x ".log.XXXXXX"

char *test_name = NULL;
bool verbose = false;
bool leave_logs = false;
bool pintos = false;
bool pathos = false;
bool use_icb = false;
bool preempt_everywhere = false;
bool pure_hb = false;
bool transactions = false;
bool abort_codes = false;

void set_job_options(char *arg_test_name, bool arg_verbose, bool arg_leave_logs,
		     bool arg_pintos, bool arg_use_icb, bool arg_preempt_everywhere,
		     bool arg_pure_hb, bool arg_txn, bool arg_txn_abort_codes,
		     bool arg_pathos)
{
	test_name = XSTRDUP(arg_test_name);
	verbose = arg_verbose;
	leave_logs = arg_leave_logs;
	pintos = arg_pintos;
	pathos = arg_pathos;
	use_icb = arg_use_icb;
	preempt_everywhere = arg_preempt_everywhere;
	pure_hb = arg_pure_hb;
	transactions = arg_txn;
	abort_codes = arg_txn_abort_codes;
}

bool testing_pintos() { return pintos; }
bool testing_pathos() { return pathos; }

struct job *new_job(struct pp_set *config, bool should_reproduce)
{
	struct job *j = XMALLOC(1, struct job);
	j->config = config;

	j->id = __sync_fetch_and_add(&job_id, 1);
	j->generation = compute_generation(config);
	j->status = JOB_NORMAL;
	j->should_reproduce = should_reproduce;

	RWLOCK_INIT(&j->stats_lock);
	j->elapsed_branches = 0;
	j->estimate_proportion = 0;
	human_friendly_time(0.0L, &j->estimate_elapsed);
	human_friendly_time(0.0L, &j->estimate_eta);
	j->estimate_eta_numeric = 0.0L;
	j->cancelled = false;
	j->complete = false;
	j->timed_out = false;
	j->kill_job = false;
	j->log_filename = NULL;
	j->trace_filename = NULL;
	j->need_rerun = false;
	j->fab_timestamp = 0;
	j->fab_cputime = 0;
	j->current_cpu = (unsigned long)-1;

	COND_INIT(&j->done_cvar);
	COND_INIT(&j->blocking_cvar);
	MUTEX_INIT(&j->lifecycle_lock);

	return j;
}

/* job thread main */
static void *run_job(void *arg)
{
	struct job *j = (struct job *)arg;
	struct messaging_state mess;

	create_file(&j->config_static,  CONFIG_STATIC_TEMPLATE);
	create_file(&j->config_dynamic, CONFIG_DYNAMIC_TEMPLATE);
	create_file(&j->log_stdout, LOG_FILE_TEMPLATE("setup"));
	create_file(&j->log_stderr, LOG_FILE_TEMPLATE("output"));

	const char *without   = pintos || pathos ? "without_function"
	                                         : "without_user_function";
	const char *mx_lock   = pintos ? "sema_down" : "mutex_lock";
	const char *mx_unlock = pintos ? "sema_up"   : "mutex_unlock";

	/* write config file */

	XWRITE(&j->config_static, "TEST_CASE=%s\n", test_name);
	XWRITE(&j->config_static, "VERBOSE=%d\n", preempt_everywhere ? 0 : verbose ? 1 : 0);
	XWRITE(&j->config_static, "ICB=%d\n", use_icb ? 1 : 0);
	XWRITE(&j->config_static, "PREEMPT_EVERYWHERE=%d\n", preempt_everywhere ? 1 : 0);
	XWRITE(&j->config_static, "PURE_HAPPENS_BEFORE=%d\n", pure_hb ? 1 : 0);

	// XXX(#120): TEST_CASE must be defined before PPs are specified.
	XWRITE(&j->config_dynamic, "TEST_CASE=%s\n", test_name);
	XWRITE(&j->config_dynamic, "%s %s\n", without, mx_lock);
	XWRITE(&j->config_dynamic, "%s %s\n", without, mx_unlock);
	if (pintos) {
		XWRITE(&j->config_dynamic, "%s %s\n", without, "intr_disable");
		XWRITE(&j->config_dynamic, "%s %s\n", without, "intr_enable");
	} else if (pathos) {
		XWRITE(&j->config_dynamic, "%s %s\n", without, "preempt_disable");
		XWRITE(&j->config_dynamic, "%s %s\n", without, "preempt_enable");
	}

	struct pp *pp;
	FOR_EACH_PP(pp, j->config) {
		XWRITE(&j->config_dynamic, "%s\n", pp->config_str);
	}

	if (pathos) {
		XWRITE(&j->config_dynamic, "%s smemalign\n", without);
		XWRITE(&j->config_dynamic, "%s sfree\n", without);
		XWRITE(&j->config_dynamic, "%s console_lock\n", without);
		XWRITE(&j->config_dynamic, "%s vm_map\n", without);
		XWRITE(&j->config_dynamic, "%s vm_free\n", without);
	} else {
		XWRITE(&j->config_dynamic, "%s malloc\n", without);
		XWRITE(&j->config_dynamic, "%s realloc\n", without);
		XWRITE(&j->config_dynamic, "%s calloc\n", without);
		XWRITE(&j->config_dynamic, "%s free\n", without);
	}

	if (pintos) {
		/* basecode sema ups/downs */
		XWRITE(&j->config_dynamic, "%s block_read\n", without);
		XWRITE(&j->config_dynamic, "%s block_write\n", without);
		XWRITE(&j->config_dynamic, "%s acquire_console\n", without);
		XWRITE(&j->config_dynamic, "%s release_console\n", without);
		XWRITE(&j->config_dynamic, "%s palloc_get_multiple\n", without);
		/* basecode clis/stis */
		XWRITE(&j->config_dynamic, "%s serial_putc\n", without);
		XWRITE(&j->config_dynamic, "%s vga_putc\n", without);
		XWRITE(&j->config_dynamic, "%s is_runqueue\n", without);
		XWRITE(&j->config_dynamic, "%s idle\n", without);
		if (0 == strcmp(test_name, "alarm-simultaneous")) {
			XWRITE(&j->config_dynamic, "%s child_done\n", without);
			XWRITE(&j->config_dynamic, "%s parent_done\n", without);
		}
	} else if (0 == strcmp(test_name, "mutex_test")) {
		// XXX: Hack. This is special cased here, instead of being a
		// cmdline option, so the studence don't have to worry about
		// setting the special flag when they run this test.
		/* When testing mutexes, add some special case config options.
		 * Ignore the innards of thr_*, and tell landslide to subject
		 * the mutex internals themselves to data race analysis. */
		XWRITE(&j->config_static, "TESTING_MUTEXES=1\n");
		XWRITE(&j->config_static, "FILTER_DRS_BY_TID=0\n");
		XWRITE(&j->config_static, "DR_PPS_RESPECT_WITHIN_FUNCTIONS=1\n");
		XWRITE(&j->config_dynamic, "%s thr_init\n", without);
		XWRITE(&j->config_dynamic, "%s thr_create\n", without);
		XWRITE(&j->config_dynamic, "%s thr_exit\n", without);
	} else if (0 == strcmp(test_name, "paraguay")) {
		XWRITE(&j->config_dynamic, "%s thr_init\n", without);
		XWRITE(&j->config_dynamic, "%s thr_create\n", without);
		XWRITE(&j->config_dynamic, "%s thr_exit\n", without);
	} else if (0 == strcmp(test_name, "paradise_lost")) {
		XWRITE(&j->config_dynamic, "%s thr_init\n", without);
		XWRITE(&j->config_dynamic, "%s thr_create\n", without);
		XWRITE(&j->config_dynamic, "%s thr_exit\n", without);
		/* this may look strange, but see the test case */
		XWRITE(&j->config_dynamic, "%s critical_section\n", without);
	} else if (transactions) {
		assert(!pintos && !pathos);
		XWRITE(&j->config_static, "HTM=1\n");
		XWRITE(&j->config_static, "FILTER_DRS_BY_TID=0\n");
		if (abort_codes) {
			XWRITE(&j->config_static, "HTM_ABORT_CODES=1\n");
		}
		XWRITE(&j->config_static, "ignore_dr_function thr_create 1\n");
		XWRITE(&j->config_static, "ignore_dr_function thr_exit 1\n");
		XWRITE(&j->config_static, "ignore_dr_function thr_join 1\n");
		// XXX: assumes sully ref p2 :(
		XWRITE(&j->config_static, "ignore_dr_function thr_bottom 1\n");
		XWRITE(&j->config_static, "ignore_dr_function wakeup_thread 1\n");
		XWRITE(&j->config_static, "ignore_dr_function remove_thread 1\n");
		XWRITE(&j->config_static, "ignore_dr_function cond_wait 1\n");
		XWRITE(&j->config_dynamic, "%s thr_init\n", without);
		XWRITE(&j->config_dynamic, "%s thr_create\n", without);
		XWRITE(&j->config_dynamic, "%s thr_exit\n", without);
		XWRITE(&j->config_dynamic, "%s thr_join\n", without);
		XWRITE(&j->config_dynamic, "%s thr_bottom\n", without);
	}

	if (preempt_everywhere) {
		XWRITE(&j->config_static, "DR_PPS_RESPECT_WITHIN_FUNCTIONS=1\n");
		if (pintos) {
			/* Manually approved shm accesses. */
			XWRITE(&j->config_dynamic, "%s intr_get_level\n", without);
			XWRITE(&j->config_dynamic, "%s intr_context\n", without);
		} else {
			/* Known offender to our ">=ebp+0x10" heuristic.
			 * See work/modules/landslide/pp.c. */
			XWRITE(&j->config_dynamic, "%s _doprnt\n", without);
		}
	}

	messaging_init(&mess, &j->config_static, &j->config_dynamic, j->id);

	// XXX: Need to do this here so the parent can have the path into pebsim
	// to properly delete the file, but it brittle-ly causes the child's
	// exec args to have "../pebsim/"s in them that only "happen to work".
	move_file_to(&j->config_static,  LANDSLIDE_PATH);
	move_file_to(&j->config_dynamic, LANDSLIDE_PATH);

	/* while multiple landslides can run at once, compiling each one from a
	 * different config is mutually exclusive. we'll release this as soon as
	 * we get a message from the child that it's up and running. */
	assert(j->current_cpu != (unsigned long)-1);
	stop_using_cpu(j->current_cpu);
	LOCK(&compile_landslide_lock);
	start_using_cpu(j->current_cpu);

	bool bug_in_subspace = bug_already_found(j->config);
	bool too_late = TIME_UP();
	if (bug_in_subspace || too_late) {
		DBG("[JOB %d] %s; aborting compilation.\n", j->id,
		    bug_in_subspace ? "bug already found" : "time ran out");
		UNLOCK(&compile_landslide_lock);
		messaging_abort(&mess);
		delete_file(&j->config_static, true);
		delete_file(&j->config_dynamic, true);
		delete_file(&j->log_stdout, true);
		delete_file(&j->log_stderr, true);
		if (bug_in_subspace) {
			WRITE_LOCK(&j->stats_lock);
			j->complete = true;
			j->cancelled = true;
			RW_UNLOCK(&j->stats_lock);
		}
		LOCK(&j->lifecycle_lock);
		j->status = JOB_DONE;
		BROADCAST(&j->done_cvar);
		UNLOCK(&j->lifecycle_lock);
		return NULL;
	}

	WRITE_LOCK(&j->stats_lock);
	j->log_filename = XSTRDUP(j->log_stderr.filename);
	j->need_rerun = false;
	RW_UNLOCK(&j->stats_lock);

	pid_t landslide_pid = fork();
	if (landslide_pid == 0) {
		/* child process; landslide-to-be */
		/* assemble commandline arguments */
		char *execname = "./" LANDSLIDE_PROGNAME;
		char *const argv[4] = {
			[0] = execname,
			[1] = j->config_static.filename,
			[2] = j->config_dynamic.filename,
			[3] = NULL,
		};

		DBG("[JOB %d] '%s %s %s > %s 2> %s'\n", j->id, execname,
		       j->config_static.filename, j->config_dynamic.filename,
		       j->log_stdout.filename, j->log_stderr.filename);

		/* unsetting cloexec not necessary for these */
		XDUP2(j->log_stdout.fd, STDOUT_FILENO);
		XDUP2(j->log_stderr.fd, STDERR_FILENO);

		XCHDIR(LANDSLIDE_PATH);

		execve(execname, argv, environ);

		EXPECT(false, "execve() failed\n");
		exit(EXIT_FAILURE);
	}

	/* parent */

	/* should take 1 to 4 seconds for child to come alive */
	bool child_alive = wait_for_child(&mess);

	UNLOCK(&compile_landslide_lock);

	if (child_alive) {
		/* may take as long as the state space is large */
		talk_to_child(&mess, j);
	} else {
		// TODO: record job in "failed to run" list or some such
		ERR("[JOB %d] There was a problem setting up Landslide.\n", j->id);
		// TODO: err_pp_set or some such
		ERR("[JOB %d] For details see %s and %s\n", j->id,
		    j->log_stdout.filename, j->log_stderr.filename);
	}

	int child_status;
	pid_t result_pid = waitpid(landslide_pid, &child_status, 0);
	assert(result_pid == landslide_pid && "wait failed");
	assert(WIFEXITED(child_status) && "wait returned before child exit");
	DBG("Landslide pid %d exited with status %d\n", landslide_pid,
	    WEXITSTATUS(child_status));

	finish_messaging(&mess);

	delete_file(&j->config_static, true);
	delete_file(&j->config_dynamic, true);
	bool should_delete = !leave_logs && WEXITSTATUS(child_status) == 0;
	delete_file(&j->log_stdout, should_delete);
	delete_file(&j->log_stderr, should_delete);

	WRITE_LOCK(&j->stats_lock);
	j->complete = true;
	if (j->need_rerun) {
		j->cancelled = true;
	}
	if (should_delete) {
		FREE(j->log_filename);
		j->log_filename = NULL;
	}
	RW_UNLOCK(&j->stats_lock);
	LOCK(&j->lifecycle_lock);
	j->status = JOB_DONE;
	BROADCAST(&j->done_cvar);
	UNLOCK(&j->lifecycle_lock);

	return NULL;
}

/* to be called by job thread of its own volition */
void job_block(struct job *j)
{
	LOCK(&j->lifecycle_lock);
	assert(j->status == JOB_NORMAL);
	j->status = JOB_BLOCKED;
	/* signal workqueue thread to go find something else to do */
	BROADCAST(&j->done_cvar);
	/* wait until there's nothing better to do */
	while (j->status == JOB_BLOCKED) {
		WAIT(&j->blocking_cvar, &j->lifecycle_lock);
	}
	/* we have been woken up and rescheduled */
	assert(j->status == JOB_NORMAL);
	UNLOCK(&j->lifecycle_lock);
}

/* the workqueue threads use the following calls to manage the job threads */

void start_job(struct job *j)
{
	pthread_t child;
	int ret = pthread_create(&child, NULL, run_job, (void *)j);
	assert(ret == 0 && "failed thread fork");
	ret = pthread_detach(child);
	assert(ret == 0 && "failed detach");
}

MUST_CHECK bool wait_on_job(struct job *j)
{
	LOCK(&j->lifecycle_lock);
	while (j->status == JOB_NORMAL) {
		WAIT(&j->done_cvar, &j->lifecycle_lock);
	}
	assert(j->status == JOB_BLOCKED || j->status == JOB_DONE);
	bool rv = j->status == JOB_BLOCKED;
	UNLOCK(&j->lifecycle_lock);
	return rv;
}

/* should be immediately followed by another call to wait_on */
void resume_job(struct job *j)
{
	LOCK(&j->lifecycle_lock);
	assert(j->status == JOB_BLOCKED);
	j->status = JOB_NORMAL;
	SIGNAL(&j->blocking_cvar);
	UNLOCK(&j->lifecycle_lock);
}

void print_job_stats(struct job *j, bool pending, bool blocked)
{
	assert(!pending || !blocked);

	READ_LOCK(&j->stats_lock);
	if (j->cancelled && !verbose) {
		RW_UNLOCK(&j->stats_lock);
		return;
	}
	PRINT("[JOB %d] ", j->id);
	if (j->cancelled) {
		PRINT(COLOUR_DARK COLOUR_YELLOW "CANCELLED");
		if (j->need_rerun) {
			PRINT(" (need rerun)");
		}
		PRINT("\n");
	} else if (j->trace_filename != NULL) {
		PRINT(COLOUR_BOLD COLOUR_RED "BUG FOUND: %s ", j->trace_filename);
		/* fab preemption count is valid even if not using ICB */
		PRINT("(%u interleaving%s tested; %u preemptions",
		      j->elapsed_branches, j->elapsed_branches == 1 ? "" : "s",
		      j->icb_fab_preemptions);
		if (verbose) {
			PRINT("; job time ");
			print_human_friendly_time(&j->estimate_elapsed);
			/* Time between start of any statespaces whatsoever
			 * until a bug was found in this one. */
			PRINT("; pldi time %lu; new-fixed pldi cputime %lu",
			      j->fab_timestamp, j->fab_cputime);
		}
		PRINT(")\n");
	} else if (j->timed_out) {
		PRINT(COLOUR_BOLD COLOUR_YELLOW "TIMED OUT ");
		PRINT("(%Lf%%; ETA ", j->estimate_proportion * 100);
		print_human_friendly_time(&j->estimate_eta);
		if (use_icb) {
			PRINT("; cur ICB bound %d", j->icb_current_bound);
		}
		PRINT(")\n");
	} else if (j->complete) {
		PRINT(COLOUR_BOLD COLOUR_GREEN "COMPLETE ");
		PRINT("(%u interleaving%s tested; ", j->elapsed_branches,
		      j->elapsed_branches == 1 ? "" : "s");
		print_human_friendly_time(&j->estimate_elapsed);
		PRINT(" elapsed");
		if (use_icb) {
			PRINT("; max ICB bound %d", j->icb_current_bound);
		}
		PRINT(")\n");
	} else if (pending) {
		PRINT("Pending...\n");
	} else if (j->elapsed_branches == 0) {
		PRINT("Setting up...\n");
	} else if (blocked) {
		PRINT(COLOUR_DARK COLOUR_MAGENTA "Deferred... ");
		PRINT("(%Lf%%; ETA ", j->estimate_proportion * 100);
		print_human_friendly_time(&j->estimate_eta);
		PRINT(")\n");
	} else {
		PRINT(COLOUR_BOLD COLOUR_MAGENTA "Running ");
		PRINT("(%Lf%%; ETA ", j->estimate_proportion * 100);
		print_human_friendly_time(&j->estimate_eta);
		if (use_icb) {
			PRINT("; cur ICB bound %d", j->icb_current_bound);
		}
		PRINT(")\n");
	}
	PRINT("       ");
	if (j->log_filename != NULL) {
		// FIXME: "id/" -- better solution for where log files should go
		PRINT(COLOUR_DARK COLOUR_GREY "Log: id/%s -- ", j->log_filename);
	}
	PRINT(COLOUR_DARK COLOUR_GREY "PPs: ");
	printf(COLOUR_GREY);
	print_pp_set(j->config, true);
	PRINT(COLOUR_DEFAULT "\n");
	RW_UNLOCK(&j->stats_lock);
}

/* Positive result = j0's ETA bigger. Negative result = j1's ETA bigger.
 * Positive result = j1's ETA better. Negative result = j0's ETA better.
 * Smaller is better. */
int compare_job_eta(struct job *j0, struct job *j1)
{
	READ_LOCK(&j0->stats_lock);
	long double eta0 = j0->estimate_eta_numeric;
	RW_UNLOCK(&j0->stats_lock);

	READ_LOCK(&j1->stats_lock);
	long double eta1 = j1->estimate_eta_numeric;
	RW_UNLOCK(&j1->stats_lock);

	return eta0 == eta1 ? 0 : eta0 < eta1 ? -1 : 1;
}
