#!/bin/bash

# @file build.sh
# @brief Outermost wrapper for the landslide build process.
# @author Ben Blum

function success {
	echo -e "\033[01;32m$1\033[00m"
}
function msg {
	echo -e "\033[01;33m$1\033[00m"
}
function err {
	echo -e "\033[01;31m$1\033[00m" >&2
}
function die {
	err "$1"
	kill $$ # may be called in backticks; exit won't work
}

function sched_func {
	echo -n
}
function ignore_sym {
	echo -n
}
function within_function {
	echo -n
}
function without_function {
	echo -n
}
function within_user_function {
	echo -n
}
function without_user_function {
	echo -n
}
function data_race {
	echo -n
}
function extra_sym {
	echo -n
}
function starting_threads {
	echo -n
}

# Doesn't work without the "./". Everything is awful forever.
if [ ! -f "./$LANDSLIDE_CONFIG" ]; then
	die "Where's $LANDSLIDE_CONFIG?"
fi
if [ ! -z "$LANDSLIDE_ID_CONFIG" ]; then
	if [ ! -f "./$LANDSLIDE_ID_CONFIG" ]; then
		die "Where's ID config $LANDSLIDE_ID_CONFIG?"
	fi
fi
TIMER_WRAPPER_DISPATCH=
IDLE_TID=
TESTING_USERSPACE=0
PREEMPT_ENABLE_FLAG=
PRINT_DATA_RACES=0
VERBOSE=0
EXTRA_VERBOSE=0
TABULAR_TRACE=0
source ./$LANDSLIDE_CONFIG

#### Check environment ####

if [ ! -d ../work/modules/landslide ]; then
	die "Where's ../work/modules/landslide?"
fi

#### Verify config options ####
function verify_nonempty {
	if [ -z "`eval echo \\$\$1`" ]; then
		die "Missing value for config option $1!"
	fi
}
function verify_numeric {
	verify_nonempty $1
	expr `eval echo \\$\$1` + 1 2>/dev/null >/dev/null
	if [ "$?" != 0 ]; then
		die "Value for $1 needs to be numeric; got \"`eval echo \\$\$1`\" instead."
	fi
}
function verify_file {
	verify_nonempty $1
	if [ ! -f "`eval echo \\$\$1`" ]; then
		die "$1 (\"`eval echo \\$\$1`\") doesn't seem to be a file."
	fi
}
function verify_dir {
	verify_nonempty $1
	if [ ! -d "`eval echo \\$\$1`" ]; then
		die "$1 (\"`eval echo \\$\$1`\") doesn't seem to be a directory."
	fi
}

verify_file KERNEL_IMG
verify_dir KERNEL_SOURCE_DIR
verify_nonempty TEST_CASE
verify_nonempty TIMER_WRAPPER
verify_nonempty CONTEXT_SWITCH
verify_nonempty READLINE
verify_numeric INIT_TID
verify_numeric SHELL_TID
verify_numeric FIRST_TID
verify_numeric BUG_ON_THREADS_WEDGED
verify_numeric EXPLORE_BACKWARDS
verify_numeric DONT_EXPLORE
verify_numeric BREAK_ON_BUG
verify_numeric TESTING_USERSPACE
verify_numeric PRINT_DATA_RACES
verify_numeric VERBOSE
verify_numeric EXTRA_VERBOSE
verify_numeric TABULAR_TRACE
if [ "$TESTING_USERSPACE" = 1 ]; then
	verify_nonempty EXEC
fi

if [ ! -z "$PREEMPT_ENABLE_FLAG" ]; then
	verify_numeric PREEMPT_ENABLE_VALUE
fi

if [ ! "$CURRENT_THREAD_LIVES_ON_RQ" = "0" ]; then
	if [ ! "$CURRENT_THREAD_LIVES_ON_RQ" = "1" ]; then
		die "CURRENT_THREAD_LIVES_ON_RQ config must be either 0 or 1."
	fi
fi

#### Check kernel image ####

if ! grep "${TEST_CASE}_exec2obj_userapp_code_ptr" $KERNEL_IMG 2>&1 >/dev/null; then
	die "Missing test program: $KERNEL_IMG isn't built with '$TEST_CASE'!"
fi

MISSING_ANNOTATIONS=
function verify_tell {
	if ! (objdump -d $KERNEL_IMG | grep "call.*$1" 2>&1 >/dev/null); then
		err "Missing annotation: $KERNEL_IMG never calls $1()"
		MISSING_ANNOTATIONS=very_yes
	fi
}

# verify_tell BdSfWtYekHgZIrgRvhjOBJZcaBOk
# verify_tell VctEddYWBoOmsvViAMLrBhgMClYkIA
# verify_tell WWCzWNfwDhCMmyOoMjkzqM
# verify_tell jEqsDtPugyVZgFYfNdUbBdjp
# verify_tell HiOxsIEPkyrBRLrxkQWOWZtnjkZ
# verify_tell qoICNoouqOWYvWrzwcMofmqGdPxa
verify_tell tell_landslide_thread_switch
verify_tell tell_landslide_sched_init_done
verify_tell tell_landslide_forking
verify_tell tell_landslide_vanishing
verify_tell tell_landslide_thread_on_rq
verify_tell tell_landslide_thread_off_rq

if [ ! -z "$MISSING_ANNOTATIONS" ]; then
	die "Please fix the missing annotations."
fi

#### Check file sanity ####

HEADER=../work/modules/landslide/student_specifics.h
STUDENT=../work/modules/landslide/student.c
SKIP_HEADER=
if [ -L $HEADER ]; then
	rm $HEADER
elif [ -f $HEADER ]; then
	if grep 'automatically generated' $HEADER 2>&1 >/dev/null; then
		MD5SUM=`grep 'image md5sum' $HEADER | sed 's/.*md5sum //' | cut -d' ' -f1`
		MD5SUM_C=`grep 'config md5sum' $HEADER | sed 's/.*md5sum //' | cut -d' ' -f1`
		MD5SUM_ID=`grep 'deepening md5sum' $HEADER | sed 's/.*md5sum //' | cut -d' ' -f1`
		MY_MD5SUM=`md5sum $KERNEL_IMG | cut -d' ' -f1`
		MY_MD5SUM_C=`md5sum ./$LANDSLIDE_CONFIG | cut -d' ' -f1`
		if [ ! -z "$LANDSLIDE_ID_CONFIG" ]; then
			MY_MD5SUM_ID=`md5sum ./$LANDSLIDE_ID_CONFIG | cut -d' ' -f1`
		else
			MY_MD5SUM_ID="NONE"
		fi
		if [ "$MD5SUM" == "$MY_MD5SUM" -a "$MD5SUM_C" == "$MY_MD5SUM_C" -a "$MD5SUM_ID" == "$MY_MD5SUM_ID" ]; then
			SKIP_HEADER=yes
		else
			rm $HEADER
		fi
	else
		die "$HEADER exists, would be clobbered; please remove/relocate it."
	fi
fi

#### Do the needful ####

msg "Generating simics config..."
./configgen.sh > landslide-config.py || die "configgen.sh failed."
if [ -z "$SKIP_HEADER" ]; then
	msg "Generating header file..."
	./definegen.sh > $HEADER || (rm -f $HEADER; die "definegen.sh failed.")
	#./definegen-obfuscated.sh > $HEADER || (rm -f $HEADER; die "definegen.sh failed.")
else
	msg "Header already generated; skipping."
fi

if [ ! -f $STUDENT ]; then
	die "$STUDENT doesn't seem to exist yet. Please implement it."
fi
(cd ../work && make) || die "Building landslide failed."
success "Build succeeded."
