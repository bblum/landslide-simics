#!/bin/bash

# this auto-generates a chunk of the kernel_specifics.c file.
# comments can be found there... (sorry)

##########################
#### helper functions ####
##########################

function get_sym {
objdump -t $KERNEL_IMG | grep " $1$" | cut -d" " -f1
}

function get_func {
objdump -d $KERNEL_IMG | grep "<$1>:" | cut -d" " -f1
}

function get_func_end {
	# original version - broken if doesn't end with ret
	#objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1 | sed 's/ //g' | cut -d":" -f1
	RET_INSTR=`objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep 'c3.*ret\|cf.*iret'`
	# If there is no ret or iret, that's probably for sched_funcs. Pretend.
	if [ -z "$RET_INSTR" ]; then
		# Use the original implementation to get the last instruction.
		objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1 | sed 's/ //g' | cut -d":" -f1
	# Test for there being only one ret or iret - normal case.
	elif [ "`echo "$RET_INSTR" | wc -l`" = "1" ]; then
		echo "$RET_INSTR" | sed 's/ //g' | cut -d":" -f1
	else
		echo "!!!"
		echo "#error \"Function $1 has multiple end-points. Read this file for details.\""
		echo "!!! You will need to hand-write multiple copies of the above macro by hand for"
		echo "!!! each one, and edit the corresponding function in kernel_specifics.c to test"
		echo "!!! for all of them. Continuing with the rest of definegen..."
		echo "!!! FEEL FREE TO ASK BEN FOR HELP."
		echo "!!!"
	fi
}

function sched_func {
echo -e "\t{ 0x`get_func $1`, 0x`get_func_end $1` }, \\"
}

function ignore_sym {
echo -e "\t{ 0x`get_sym $1`, $2 }, \\"
}

#############################
#### Reading user config ####
#############################

# Doesn't work without the "./". Everything is awful forever.
CONFIG=./config.landslide
if [ ! -f "$CONFIG" ]; then
	echo "Where's $CONFIG?"
	exit 1
fi
TIMER_WRAPPER_DISPATCH=
source $CONFIG

#####################################
#### User config sanity checking ####
#####################################

if [ ! -f "$KERNEL_IMG" ]; then
	echo "invalid kernel image specified"
	exit 1
fi
if [ -z "$KERNEL_NAME" ]; then
	echo "what is the name of this kernel?"
	exit 1
fi

# TODO: add more

##################
#### Begin... ####
##################

KERNEL_NAME_LOWER=`echo $KERNEL_NAME | tr '[:upper:]' '[:lower:]'`
KERNEL_NAME_UPPER=`echo $KERNEL_NAME | tr '[:lower:]' '[:upper:]'`

echo "/**"
echo " * @file kernel_specifics_$KERNEL_NAME_LOWER.h"
echo " * @brief #defines for the $KERNEL_NAME guest kernel (automatically generated)"
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
echo "#define GUEST_ESP0(cpu) READ_MEMORY(cpu, GUEST_ESP0_ADDR)"

# XXX: deal with this
echo "#define GUEST_TCB_STATE_FLAG_OFFSET 20" # for off_runqueue

###################################
#### Thread lifecycle tracking ####
###################################

echo

if [ ! -z "$TIMER_WRAPPER_DISPATCH" ]; then
	# Difficult case, but handled.
	TIMER_WRAP_EXIT=`get_func_end $TIMER_WRAPPER_DISPATCH`
else
	# check the end instruction for being ret, and spit out a "ask ben for help" warning
	LAST_TIMER_INSTR=`objdump -d $KERNEL_IMG | grep -A10000 "<$TIMER_WRAPPER>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1`
	if echo "$LAST_TIMER_INSTR" | grep -v jmp | grep '\<iret\>' 2>&1 >/dev/null; then
		# Easy case.
		TIMER_WRAP_EXIT=`get_func_end $TIMER_WRAPPER`
	else
		# Difficult case, not handled.
		echo "!!!!"
		echo "Something is funny about your timer handler, and Ben expected this to happen."
		echo "Expected the last instruction to be 'iret', but got:"
		echo "$LAST_TIMER_INSTR"
		echo "Ask Ben for help."
		echo "!!!!"
		exit 1
	fi
fi

echo "#define GUEST_TIMER_WRAP_ENTER     0x`get_func $TIMER_WRAPPER`"
echo "#define GUEST_TIMER_WRAP_EXIT      0x$TIMER_WRAP_EXIT"

echo "#define GUEST_CONTEXT_SWITCH_ENTER 0x`get_func $CONTEXT_SWITCH`"
echo "#define GUEST_CONTEXT_SWITCH_EXIT  0x`get_func_end $CONTEXT_SWITCH`"

echo

# TODO - find some way to kill this
echo "#define GUEST_FORK_RETURN_SPOT     0x`get_sym get_to_userspace`"

# Readline
echo "#define GUEST_READLINE_WINDOW_ENTER 0x`get_func $READLINE`"
echo "#define GUEST_READLINE_WINDOW_EXIT 0x`get_func_end $READLINE`"

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
echo "#define GUEST_LMM_ALLOC_EXIT       0x`get_func_end lmm_alloc`"
echo "#define GUEST_LMM_ALLOC_SIZE_ARGNUM 2"
echo "#define GUEST_LMM_ALLOC_GEN_ENTER  0x`get_func lmm_alloc_gen`"
echo "#define GUEST_LMM_ALLOC_GEN_EXIT   0x`get_func_end lmm_alloc_gen`"
echo "#define GUEST_LMM_ALLOC_GEN_SIZE_ARGNUM 2"
echo "#define GUEST_LMM_FREE_ENTER       0x`get_func lmm_free`"
echo "#define GUEST_LMM_FREE_EXIT        0x`get_func_end lmm_free`"
echo "#define GUEST_LMM_FREE_BASE_ARGNUM 2"
echo "#define GUEST_LMM_FREE_SIZE_ARGNUM 3"
echo "#define GUEST_LMM_REMOVE_FREE_ENTER 0x`get_func lmm_remove_free`"
echo "#define GUEST_LMM_REMOVE_FREE_EXIT 0x`get_func_end lmm_remove_free`"

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
echo "#define GUEST_OUTB 0x`get_func outb`"

echo

###############################
#### In-kernel annotations ####
###############################

echo "#define TELL_LANDSLIDE_THREAD_SWITCH 0x`get_func tell_landslide_thread_switch`"
echo "#define TELL_LANDSLIDE_SCHED_INIT_DONE 0x`get_func tell_landslide_sched_init_done`"
echo "#define TELL_LANDSLIDE_FORKING 0x`get_func tell_landslide_forking`"
echo "#define TELL_LANDSLIDE_VANISHING 0x`get_func tell_landslide_vanishing`"
echo "#define TELL_LANDSLIDE_SLEEPING 0x`get_func tell_landslide_sleeping`"
echo "#define TELL_LANDSLIDE_THREAD_RUNNABLE 0x`get_func tell_landslide_thread_runnable`"
echo "#define TELL_LANDSLIDE_THREAD_DESCHEDULING 0x`get_func tell_landslide_thread_descheduling`"
echo "#define TELL_LANDSLIDE_MUTEX_LOCKING 0x`get_func tell_landslide_mutex_locking`"
echo "#define TELL_LANDSLIDE_MUTEX_BLOCKING 0x`get_func tell_landslide_mutex_blocking`"
echo "#define TELL_LANDSLIDE_MUTEX_LOCKING_DONE 0x`get_func tell_landslide_mutex_locking_done`"
echo "#define TELL_LANDSLIDE_MUTEX_UNLOCKING 0x`get_func tell_landslide_mutex_unlocking`"
echo "#define TELL_LANDSLIDE_MUTEX_UNLOCKING_DONE 0x`get_func tell_landslide_mutex_unlocking_done`"

echo

##############################
#### Scheduler boundaries ####
##############################

# Things that are likely to always touch shared memory that you don't care about
# such as runqueue links, but not those that the globals list filters out, such
# as links inside each tcb.
echo "#define GUEST_SCHEDULER_FUNCTIONS { \\"
sched_funcs
echo -e "\t}"

echo "#define GUEST_SCHEDULER_GLOBALS { \\"
ignore_syms
echo -e "\t}"

echo

#######################
#### Choice points ####
#######################

# XXX
echo "#define GUEST_MUTEX_LOCK 0x`get_func mutex_lock`"
echo "#define GUEST_VANISH 0x`get_func do_vanish`"
echo "#define GUEST_VANISH_END 0x`get_func_end do_vanish`"
echo "#define GUEST_MUTEX_LOCK_MUTEX_ARGNUM 1"

echo "#define GUEST_MUTEX_IGNORES { \\"
ignore_mutexes
echo -e "\t}"

echo

echo "#endif"
