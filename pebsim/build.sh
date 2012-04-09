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
function extra_sym {
	echo -n
}
function starting_threads {
	echo -n
}

# Doesn't work without the "./". Everything is awful forever.
CONFIG=./config.landslide
if [ ! -f "$CONFIG" ]; then
	die "Where's $CONFIG?"
fi
TIMER_WRAPPER_DISPATCH=
IDLE_TID=
source $CONFIG

if [ ! -f "$KERNEL_IMG" ]; then
	die "invalid kernel image specified: KERNEL_IMG=$KERNEL_IMG"
fi

#### Check environment ####

if [ "`pwd | sed 's/.*\///'`" != "pebsim" ]; then
	die "$0 must be run from the pebsim directory."
fi

if [ ! -d ../work/modules/landslide ]; then
	die "Where's ../work/modules/landslide?"
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
		MY_MD5SUM=`md5sum $KERNEL_IMG | cut -d' ' -f1`
		MY_MD5SUM_C=`md5sum $CONFIG | cut -d' ' -f1`
		if [ "$MD5SUM" == "$MY_MD5SUM" -a "$MD5SUM_C" == "$MY_MD5SUM_C" ]; then
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
else
	msg "Header already generated; skipping."
fi

if [ ! -f $STUDENT ]; then
	die "$STUDENT doesn't seem to exist yet. Please implement it."
fi
(cd ../work && make) || die "Building landslide failed."
success "Build succeeded. Run \"time ./simics4\" when you are ready."
