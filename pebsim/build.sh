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

# If some part of the setup process fails before landslide can send the
# thunderbirds message, we need to not leave the master hanging (literally).
OUTPUT_PIPE=
function die {
	err "$1"
	if [ ! -z "$OUTPUT_PIPE" ]; then
		echo -n > $OUTPUT_PIPE
	fi
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
function ignore_dr_function {
	echo -n
}
function data_race {
	echo -n
}
function disk_io_func {
	echo -n
}
function extra_sym {
	echo -n
}
function starting_threads {
	echo -n
}
function id_magic {
	echo -n
}
INPUT_PIPE=
function input_pipe {
	INPUT_PIPE=$1
}

OUTPUT_PIPE=
function output_pipe {
	OUTPUT_PIPE=$1
}

# Doesn't work without the "./". Everything is awful forever.
if [ ! -f "./$LANDSLIDE_CONFIG" ]; then
	die "Where's $LANDSLIDE_CONFIG?"
fi
TIMER_WRAPPER_DISPATCH=
IDLE_TID=
TESTING_USERSPACE=0
PREEMPT_ENABLE_FLAG=
PRINT_DATA_RACES=0
VERBOSE=0
EXTRA_VERBOSE=0
TABULAR_TRACE=0
OBFUSCATED_KERNEL=0
PINTOS_KERNEL=
source ./$LANDSLIDE_CONFIG

if [ ! -z "$LANDSLIDE_ID_CONFIG" ]; then
	if [ ! -f "./$LANDSLIDE_ID_CONFIG" ]; then
		die "Where's ID config $LANDSLIDE_ID_CONFIG?"
	fi
	source "$LANDSLIDE_ID_CONFIG"
fi

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
if [ -z "$PINTOS_KERNEL" ]; then
	verify_nonempty READLINE
	verify_numeric SHELL_TID
fi
verify_numeric INIT_TID
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

if [ ! "$OBFUSCATED_KERNEL" = 0 ]; then
	if [ ! "$OBFUSCATED_KERNEL" = 1 ]; then
		die "Invalid value for OBFUSCATED_KERNEL; have '$OBFUSCATED_KERNEL'; need 0/1"
	elif [ ! -z "$PINTOS_KERNEL" ]; then
		die "PINTOS_KERNEL and OBFUSCATED_KERNEL are incompatible."
	fi
fi

if [ -z "$PINTOS_KERNEL" ]; then
	# Pebbles
	if ! grep "${TEST_CASE}_exec2obj_userapp_code_ptr" $KERNEL_IMG 2>&1 >/dev/null; then
		die "Missing test program: $KERNEL_IMG isn't built with '$TEST_CASE'!"
	fi
else
	# Pintos. Verify
	msg "Verifying bootfd built to run $TEST_CASE."
	cd pintos || die "couldn't cd pintos"
	./make-bootfd.sh "$TEST_CASE" || die "couldn't remake bootfd"
	cp bootfd.img .. || die "made bootfd but failed cp"
	cd .. || die "?????.... ?"
fi

MISSING_ANNOTATIONS=
function verify_tell {
	if ! (objdump -d $KERNEL_IMG | grep "call.*$1" 2>&1 >/dev/null); then
		err "Missing annotation: $KERNEL_IMG never calls $1()"
		MISSING_ANNOTATIONS=very_yes
	fi
}

source ./symbols.sh

verify_tell "$TL_FORKING"
verify_tell "$TL_OFF_RQ"
verify_tell "$TL_ON_RQ"
verify_tell "$TL_SWITCH"
verify_tell "$TL_INIT_DONE"
verify_tell "$TL_VANISH"

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
			rm -f $HEADER || die "Couldn't overwrite existing header $HEADER"
		fi
	elif [ ! -z "$LANDSLIDE_ID_CONFIG" ]; then
		# Run from ID wrapper. Attempt to silently clobber the existing header.
		rm -f "$HEADER" || die "Attempted to silently clobber $HEADER but failed!"
	else
		# Run in manual mode. Let user know about header problem.
		die "$HEADER exists, would be clobbered; please remove/relocate it."
	fi
fi

#### Accept simics-4.0 license if not already ####

SIMICS_PREFS_DIR="$HOME/.simics/4.0"
SIMICS_PREFS="$SIMICS_PREFS_DIR/prefs"
mkdir -p $SIMICS_PREFS_DIR
touch "$SIMICS_PREFS" || die "can't access simics-4.0 prefs file @ $SIMICS_PREFS??"

if ! grep "acad_sla_accepted: TRUE" "$SIMICS_PREFS" >/dev/null 2>/dev/null; then
	msg "Automatically accepting Simics 4.0 SLA for you..."
	cp ./simics-4.0-prefs "$SIMICS_PREFS" || die "couldn't copy over simics prefs to $SIMICS_PREFS??"
fi

#### Do the needful ####

msg "Generating simics config..."
./configgen.sh > landslide-config.py || die "configgen.sh failed."
if [ -z "$SKIP_HEADER" ]; then
	msg "Generating header file..."
	./definegen.sh > $HEADER || (rm -f $HEADER; die "definegen.sh failed.")
else
	msg "Header already generated; skipping."
fi

if [ ! -f $STUDENT ]; then
	die "$STUDENT doesn't seem to exist yet. Please implement it."
fi
# XXX FIXME: Shouldn't need to 'make clean' here. But trying to hack around bug #114.
(cd ../work && make clean && make) || die "Building landslide failed."
success "Build succeeded."
