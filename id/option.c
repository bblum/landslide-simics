/**
 * @file option.c
 * @brief command line options
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#define _XOPEN_SOURCE 700

#include <ctype.h> /* isprint */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h> /* get_nprocs */
#include <unistd.h> /* getopt */

#include "array_list.h"
#include "common.h"
#include "option.h"

#define MINTIME ((unsigned long)600) /* 10 mins */
#define DEFAULT_TIME "1h"

#define DEFAULT_TEST_CASE "thread_exit_join"

struct cmdline_option {
	char flag;
	bool requires_arg;
	char *name; /* iff requires_arg */
	char *description;
	char *default_value; /* iff requires arg */
	char **value_ptr; /* iff requires_arg */
	bool *bool_ptr; /* iff NOT requires_arg */
};

static ARRAY_LIST(struct cmdline_option) cmdline_options;
static bool ready = false;

void usage(char *execname)
{
	assert(ready);
	printf(COLOUR_BOLD "Usage: %s ", execname);
	unsigned int i;
	struct cmdline_option *optp;
	ARRAY_LIST_FOREACH(&cmdline_options, i, optp) {
		if (optp->requires_arg) {
			printf("[-%c %s] ", optp->flag, optp->name);
		} else {
			printf("[-%c] ", optp->flag);
		}
	}
	printf("\n");

	ARRAY_LIST_FOREACH(&cmdline_options, i, optp) {
		if (optp->requires_arg) {
			printf("\t%s:\t%s (default %s)\n", optp->name,
			     optp->description, optp->default_value);
		} else {
			printf("\t-%c:\t\t%s\n", optp->flag, optp->description);
		}
	}
	printf(COLOUR_DEFAULT);
}

static bool parse_time(char *str, unsigned long *result)
{
	char *endp;
	long time = strtol(str, &endp, 0);
	if (errno != 0) {
		ERR("Time must be a number (got '%s')\n", str);
		return false;
	}
	if (time < 0) {
		ERR("Cannot time travel\n");
		return false;
	}
	*result = (unsigned long)time;
	switch (*endp) {
		case 'y':
			WARN("%ld year%s, are you sure?\n", *result,
			     *result == 1 ? "" : "s");
			*result *= 365;
		case 'd':
			*result *= 24;
		case 'h':
			*result *= 60;
		case 'm':
			*result *= 60;
		case '\0':
		case 's':
			break;
		default:
			ERR("Unrecognized time format '%s'\n", str);
			return false;
	}
	return true;
}

bool get_options(int argc, char **argv, char *test_name, unsigned int test_name_len,
		 unsigned long *max_time, unsigned long *num_cpus, bool *verbose)
{
	/* Set up cmdline options & their default values */
	unsigned int system_cpus = get_nprocs();
	assert(system_cpus > 0);
	char all_but_one_cpus[BUF_SIZE];
	scnprintf(all_but_one_cpus, BUF_SIZE, "%u",
		  MAX((unsigned int)1, system_cpus - 1));

	ARRAY_LIST_INIT(&cmdline_options, 16);

	char getopt_buf[BUF_SIZE];
	getopt_buf[0] = '\0';

#define DEF_CMDLINE_FLAG(flagname, varname, descr)			\
	bool arg_##varname = false;					\
	/* define global option struct */				\
	struct cmdline_option __arg_##varname##_struct;			\
	__arg_##varname##_struct.flag          = flagname;		\
	__arg_##varname##_struct.requires_arg  = false;			\
	__arg_##varname##_struct.name          = NULL;			\
	__arg_##varname##_struct.default_value = NULL;			\
	__arg_##varname##_struct.description   = XSTRDUP(descr);	\
	__arg_##varname##_struct.value_ptr     = NULL;			\
	__arg_##varname##_struct.bool_ptr      = &arg_##varname;	\
	ARRAY_LIST_APPEND(&cmdline_options, __arg_##varname##_struct);	\
	/* append getopt_buf with flagname */				\
	unsigned int __buf_index_##varname = strlen(getopt_buf);	\
	assert(__buf_index_##varname + 1 < BUF_SIZE);			\
	getopt_buf[__buf_index_##varname] = flagname;			\
	getopt_buf[__buf_index_##varname + 1] = '\0';			\

	DEF_CMDLINE_FLAG('v', verbose, "Verbose output");
	DEF_CMDLINE_FLAG('h', help, "Print this help text and exit");
#undef DEF_CMDLINE_FLAG

#define DEF_CMDLINE_OPTION(flagname, varname, descr, value)		\
	char *arg_##varname = value;					\
	/* define global option struct */				\
	struct cmdline_option __arg_##varname##_struct;			\
	__arg_##varname##_struct.flag          = flagname;		\
	__arg_##varname##_struct.requires_arg  = true;			\
	__arg_##varname##_struct.name          = XSTRDUP(#varname);	\
	__arg_##varname##_struct.default_value = XSTRDUP(value);	\
	__arg_##varname##_struct.description   = XSTRDUP(descr);	\
	__arg_##varname##_struct.value_ptr     = &arg_##varname;	\
	__arg_##varname##_struct.bool_ptr      = NULL;			\
	ARRAY_LIST_APPEND(&cmdline_options, __arg_##varname##_struct);	\
	/* append getopt_buf with flagname (and a ":") */		\
	unsigned int __buf_index_##varname = strlen(getopt_buf);	\
	assert(__buf_index_##varname + 2 < BUF_SIZE);			\
	getopt_buf[__buf_index_##varname] = flagname;			\
	getopt_buf[__buf_index_##varname + 1] = ':';			\
	getopt_buf[__buf_index_##varname + 2] = '\0';			\

	DEF_CMDLINE_OPTION('p', test_name, "Userspace test program name", DEFAULT_TEST_CASE);
	DEF_CMDLINE_OPTION('t', max_time, "Total CPU time budget (suffix s/m/d/h/y)", DEFAULT_TIME);
	DEF_CMDLINE_OPTION('c', num_cpus, "Max parallelism factor", all_but_one_cpus);
#undef DEF_CMDLINE_OPTION

	ready = true;

	bool options_valid = true;
	int option_char;
	unsigned int i;
	struct cmdline_option *optp;
	while ((option_char = getopt(argc, argv, getopt_buf)) != -1) {
		bool found = false;
		if (option_char == '?') {
			options_valid = false;
			break;
		}
		ARRAY_LIST_FOREACH(&cmdline_options, i, optp) {
			if (option_char == optp->flag) {
				if (optp->requires_arg) {
					*optp->value_ptr = optarg;
				} else {
					*optp->bool_ptr = true;
				}
				found = true;
				break;
			}
		}
		if (!found) {
			if (isprint(option_char)) {
				WARN("Unrecognized option '%c'\n", option_char);
			} else {
				WARN("Unrecognized option 0x%x\n", option_char);
			}
			options_valid = false;
		}
	}
	
	/* unset dangerous value_ptr field */
	ARRAY_LIST_FOREACH(&cmdline_options, i, optp) {
		optp->value_ptr = NULL;
		optp->bool_ptr  = NULL;
	}

	/* Interpret string versions of arguments (or their defaults) */

	if (!parse_time(arg_max_time, max_time)) {
		options_valid = false;
	} else if (*max_time >= ULONG_MAX / 1000000) {
		ERR("%ld seconds is too much time for unsigned long\n",
		    *max_time);
		options_valid = false;
	} else if (*max_time < MINTIME) {
		WARN("%ld seconds (%s) not enough time; defaulting to %ld\n",
		     *max_time, arg_max_time, MINTIME);
		*max_time = MINTIME;
	}

	*num_cpus = strtol(arg_num_cpus, NULL, 0);
	if (errno != 0) {
		ERR("num_cpus must be a number (got '%s')\n", arg_num_cpus);
		options_valid = false;
	} else if (*num_cpus == 0) {
		ERR("Cannot use 0 CPUs (%u CPUs available)\n", system_cpus);
		options_valid = false;
	} else if (*num_cpus > system_cpus) {
		WARN("%lu CPUs is too many; we can only use %u\n",
		     *num_cpus, system_cpus);
		*num_cpus = system_cpus;
	}

	if (arg_help) {
		options_valid = false;
	}

	scnprintf(test_name, test_name_len, "%s", arg_test_name);

	*verbose = arg_verbose;

	return options_valid;
}
