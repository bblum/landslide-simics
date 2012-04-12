#!/bin/bash

# this auto-generates a chunk of the kernel_specifics.c file.
# comments can be found there... (sorry)

##########################
#### helper functions ####
##########################

function err {
	echo -e "\033[01;31m$1\033[00m" >&2
}
function die {
	err "$1"
	kill $$ # may be called in backticks; exit won't work
}

function get_sym {
	RESULT=`objdump -t $KERNEL_IMG | grep " $1$" | cut -d" " -f1`
	if [ -z "$RESULT" ]; then
		die "Couldn't find symbol $1."
	fi
	echo $RESULT
}

function get_func {
	RESULT=`objdump -d $KERNEL_IMG | grep "<$1>:" | cut -d" " -f1`
	if [ -z "$RESULT" ]; then
		die "Couldn't find function $1."
	fi
	echo $RESULT
}

# Gets the last instruction, spatially. Might not be ret or iret.
function get_func_end {
	RESULT=`objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1 | sed 's/ //g' | cut -d":" -f1`
	if [ -z "$RESULT" ]; then
		die "Couldn't find end-of-function $1."
	fi
	echo $RESULT
}

# Gets the last instruction, temporally. Must be ret or iret.
function get_func_ret {
	RET_INSTR=`objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep 'c3.*ret\|cf.*iret'`
	# Test for there being only one ret or iret - normal case.
	if [ "`echo "$RET_INSTR" | wc -l`" = "1" ]; then
		echo "$RET_INSTR" | sed 's/ //g' | cut -d":" -f1
	else
		err "!!!"
		err "!!! Function $1 has multiple end-points."
		err "!!! You will need to hand-write multiple copies of the above macro by hand for"
		err "!!! each one, and edit the corresponding function in kernel_specifics.c to test"
		err "!!! for all of them."
		err "!!! PLEASE ASK BEN FOR HELP."
		die "!!!"
	fi
}

SCHED_FUNCS=
function sched_func {
	SCHED_FUNCS="${SCHED_FUNCS}\\\\\n\t{ 0x`get_func $1`, 0x`get_func_end $1` },"
}

IGNORE_SYMS=
function ignore_sym {
	IGNORE_SYMS="${IGNORE_SYMS}\\\\\n\t{ 0x`get_sym $1`, $2 }, "
}

EXTRA_SYMS=
function extra_sym {
	EXTRA_SYMS="${EXTRA_SYMS}\n#define $2 0x`get_sym $1`"
}

WITHIN_FUNCTIONS=
# The list will appear in order that they are called.
# Later ones specified take precedence (as per the implementation in kernel_specifics.c).
function within_function {
	WITHIN_FUNCTIONS="${WITHIN_FUNCTIONS}\\\\\n\t{ 0x`get_func $1`, 0x`get_func_end $1`, 1 },"
}
function without_function {
	WITHIN_FUNCTIONS="${WITHIN_FUNCTIONS}\\\\\n\t{ 0x`get_func $1`, 0x`get_func_end $1`, 0 },"
}

STARTING_THREADS=
function starting_threads {
	if [ -z "$1" -o -z "$2" ]; then
		die "starting_threads needs two args: got \"$1\" and \"$2\""
	elif [ "$2" != "1" -a "$2" != "0" ]; then
		die "starting_threads \"$1\" \"$2\" - expected '1' or '0' for 2nd arg"
	fi
	STARTING_THREADS="${STARTING_THREADS} add_thread(s, $1, $2);"
}

#############################
#### Reading user config ####
#############################

# Doesn't work without the "./". Everything is awful forever.
CONFIG=./config.landslide
if [ ! -f "$CONFIG" ]; then
	die "Where's $CONFIG?"
fi
TIMER_WRAPPER_DISPATCH=
IDLE_TID=
CONTEXT_SWITCH_RETURN=
source $CONFIG

#####################################
#### User config sanity checking ####
#####################################

if [ ! -f "$KERNEL_IMG" ]; then
	die "invalid kernel image specified: KERNEL_IMG=$KERNEL_IMG"
fi
if [ -z "$KERNEL_NAME" ]; then
	KERNEL_NAME=$USER
fi

##################
#### Begin... ####
##################

KERNEL_NAME_LOWER=`echo $KERNEL_NAME | tr '[:upper:]' '[:lower:]'`
KERNEL_NAME_UPPER=`echo $KERNEL_NAME | tr '[:lower:]' '[:upper:]'`

echo "/**"
echo " * @file kernel_specifics_$KERNEL_NAME_LOWER.h"
echo " * @brief #defines for the $KERNEL_NAME guest kernel (automatically generated)"
echo " * Built for image md5sum `md5sum $KERNEL_IMG`"
echo " * Using config md5sum `md5sum $CONFIG`"
echo " * @author Ben Blum <bblum@andrew.cmu.edu>"
echo " */"
echo
echo "#ifndef __LS_KERNEL_SPECIFICS_${KERNEL_NAME_UPPER}_H"
echo "#define __LS_KERNEL_SPECIFICS_${KERNEL_NAME_UPPER}_H"
echo
echo "#include \"x86.h\""
echo

########################
#### TCB management ####
########################

echo "#define GUEST_ESP0_ADDR (0x`get_sym init_tss` + 4)" # see 410kern/x86/asm.S
echo "#define GET_ESP0(cpu) READ_MEMORY(cpu, GUEST_ESP0_ADDR)"

###################################
#### Thread lifecycle tracking ####
###################################

echo

if [ ! -z "$TIMER_WRAPPER_DISPATCH" ]; then
	# Difficult case, but handled.
	TIMER_WRAP_EXIT=`get_func_ret $TIMER_WRAPPER_DISPATCH`
else
	# check the end instruction for being ret, and spit out a "ask ben for help" warning
	LAST_TIMER_INSTR=`objdump -d $KERNEL_IMG | grep -A10000 "<$TIMER_WRAPPER>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1`
	if echo "$LAST_TIMER_INSTR" | grep -v jmp | grep '\<iret\>' 2>&1 >/dev/null; then
		# Easy case.
		TIMER_WRAP_EXIT=`get_func_ret $TIMER_WRAPPER`
	else
		# Difficult case, not handled.
		err "!!!!"
		err "Something is funny about your timer handler, and Ben expected this to happen."
		err "Expected the last instruction to be 'iret', but got:"
		err "$LAST_TIMER_INSTR"
		err "Ask Ben for help."
		die "!!!!"
	fi
fi

echo "#define GUEST_TIMER_WRAP_ENTER     0x`get_func $TIMER_WRAPPER`"
echo "#define GUEST_TIMER_WRAP_EXIT      0x$TIMER_WRAP_EXIT"

if [ ! -z "$CONTEXT_SWITCH_RETURN" ]; then
	CONTEXT_SWITCH_EXIT=`get_func_ret $CONTEXT_SWITCH_RETURN`
else
	CONTEXT_SWITCH_EXIT=`get_func_ret $CONTEXT_SWITCH`
fi

echo "#define GUEST_CONTEXT_SWITCH_ENTER 0x`get_func $CONTEXT_SWITCH`"
echo "#define GUEST_CONTEXT_SWITCH_EXIT  0x$CONTEXT_SWITCH_EXIT"

echo

# Readline
echo "#define GUEST_READLINE_WINDOW_ENTER 0x`get_func $READLINE`"
echo "#define GUEST_READLINE_WINDOW_EXIT 0x`get_func_ret $READLINE`"

echo

######################################
#### Mutexes / Deadlock detection ####
######################################

# Moved to annotations

###################################
#### Dynamic memory allocation ####
###################################

# Pebbles user shouldn't need to change this.
echo "#define GUEST_LMM_ALLOC_ENTER      0x`get_func lmm_alloc`"
echo "#define GUEST_LMM_ALLOC_EXIT       0x`get_func_ret lmm_alloc`"
echo "#define GUEST_LMM_ALLOC_SIZE_ARGNUM 2"
echo "#define GUEST_LMM_ALLOC_GEN_ENTER  0x`get_func lmm_alloc_gen`"
echo "#define GUEST_LMM_ALLOC_GEN_EXIT   0x`get_func_ret lmm_alloc_gen`"
echo "#define GUEST_LMM_ALLOC_GEN_SIZE_ARGNUM 2"
echo "#define GUEST_LMM_FREE_ENTER       0x`get_func lmm_free`"
echo "#define GUEST_LMM_FREE_EXIT        0x`get_func_ret lmm_free`"
echo "#define GUEST_LMM_FREE_BASE_ARGNUM 2"
echo "#define GUEST_LMM_FREE_SIZE_ARGNUM 3"
echo "#define GUEST_LMM_REMOVE_FREE_ENTER 0x`get_func lmm_remove_free`"
echo "#define GUEST_LMM_REMOVE_FREE_EXIT 0x`get_func_ret lmm_remove_free`"

echo

#############################################
#### Kernel image regions / misc symbols ####
#############################################

# Pebbles user shouldn't need to change this.
echo "#define GUEST_IMG_END 0x`get_sym _end`"
echo "#define GUEST_DATA_START 0x`get_sym .data`"
echo "#define GUEST_DATA_END 0x`get_sym _edata`" # Everything is awful forever.
echo "#define GUEST_BSS_START 0x`get_sym __bss_start`"
echo "#define GUEST_BSS_END GUEST_IMG_END"

echo "#define GUEST_PANIC 0x`get_func panic`"
echo "#define GUEST_KERNEL_MAIN 0x`get_func kernel_main`"

echo

###############################
#### In-kernel annotations ####
###############################

echo "#define TELL_LANDSLIDE_DECIDE 0x`get_func tell_landslide_decide`"
echo "#define TELL_LANDSLIDE_THREAD_SWITCH 0x`get_func tell_landslide_thread_switch`"
echo "#define TELL_LANDSLIDE_SCHED_INIT_DONE 0x`get_func tell_landslide_sched_init_done`"
echo "#define TELL_LANDSLIDE_FORKING 0x`get_func tell_landslide_forking`"
echo "#define TELL_LANDSLIDE_VANISHING 0x`get_func tell_landslide_vanishing`"
echo "#define TELL_LANDSLIDE_SLEEPING 0x`get_func tell_landslide_sleeping`"
echo "#define TELL_LANDSLIDE_THREAD_RUNNABLE 0x`get_func tell_landslide_thread_on_rq`"
echo "#define TELL_LANDSLIDE_THREAD_DESCHEDULING 0x`get_func tell_landslide_thread_off_rq`"
echo "#define TELL_LANDSLIDE_MUTEX_LOCKING 0x`get_func tell_landslide_mutex_locking`"
echo "#define TELL_LANDSLIDE_MUTEX_BLOCKING 0x`get_func tell_landslide_mutex_blocking`"
echo "#define TELL_LANDSLIDE_MUTEX_LOCKING_DONE 0x`get_func tell_landslide_mutex_locking_done`"
echo "#define TELL_LANDSLIDE_MUTEX_UNLOCKING 0x`get_func tell_landslide_mutex_unlocking`"
echo "#define TELL_LANDSLIDE_MUTEX_UNLOCKING_DONE 0x`get_func tell_landslide_mutex_unlocking_done`"

echo

##########################
#### Scheduler stuffs ####
##########################

# Things that are likely to always touch shared memory that you don't care about
# such as runqueue links, but not those that the globals list filters out, such
# as links inside each tcb.
echo -e "#define GUEST_SCHEDULER_FUNCTIONS { $SCHED_FUNCS }"

echo -e "#define GUEST_SCHEDULER_GLOBALS { $IGNORE_SYMS }"

echo

echo "#define GUEST_INIT_TID $INIT_TID"
echo "#define GUEST_SHELL_TID $SHELL_TID"
echo "#define GUEST_FIRST_TID $FIRST_TID"
if [ -z "$IDLE_TID" ]; then
	echo "#ifdef GUEST_IDLE_TID"
	echo "#undef GUEST_IDLE_TID"
	echo "#endif"
else
	echo "#define GUEST_IDLE_TID $IDLE_TID"
fi

echo "#define GUEST_STARTING_THREAD_CODE do {$STARTING_THREADS } while (0)"

###########################
#### User-config stuff ####
###########################

echo -e "$EXTRA_SYMS"

#######################
#### Choice points ####
#######################

echo -e "#define GUEST_WITHIN_FUNCTIONS { $WITHIN_FUNCTIONS }"

echo "#define BUG_ON_THREADS_WEDGED $BUG_ON_THREADS_WEDGED"
echo "#define EXPLORE_BACKWARDS $EXPLORE_BACKWARDS"
echo "#define DECISION_INFO_ONLY $DECISION_INFO_ONLY"
echo "#define BREAK_ON_BUG $BREAK_ON_BUG"

echo

echo "#endif"
