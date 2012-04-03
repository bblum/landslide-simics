#!/bin/bash

# @file configgen.sh
# @brief Generates landslide-config.py for config.simics

function sched_func {
	echo -n
}
function ignore_sym {
	echo -n
}
function within_function {
	echo -n
}
function extra_sym {
	echo -n
}

function die {
	echo -e "\033[01;31m$1\033[00m" >&2
	exit 1
}

# Doesn't work without the "./". Everything is awful forever.
CONFIG=./config.landslide
if [ ! -f "$CONFIG" ]; then
	die "Where's $CONFIG?"
fi
TIMER_WRAPPER_DISPATCH=
IDLE_TID=
source $CONFIG

# Check sanity

if [ ! -f "$KERNEL_IMG" ]; then
	die "Invalid kernel image $KERNEL_IMG"
fi
if [ ! -d "$KERNEL_SOURCE_DIR" ]; then
	die "Invalid kernel source dir $KERNEL_SOURCE_DIR"
fi

if [ -L kernel ]; then
	rm kernel
elif [ -f kernel -o -d kernel ]; then
	die "'kernel' exists, would be clobbered, please remove/relocate it."
fi

ln -s $KERNEL_IMG kernel

# Generate file

echo "ls_source_path = \"$KERNEL_SOURCE_DIR\""
echo "ls_test_case = \"$TEST_CASE\""
