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
	objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1 | sed 's/ //g' | cut -d":" -f1
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

# XXX: deal with this
echo "#define GUEST_ESP0_ADDR (0x`get_sym init_tss` + 4)" # see 410kern/x86/asm.S
echo "#define GUEST_ESP0(cpu) READ_MEMORY(cpu, GUEST_ESP0_ADDR)"

# TODO: make like the predecessor of that which infected mushroom cooks, and generalised it
echo "#define GUEST_TCB_TID_OFFSET 8"
echo "#define TID_FROM_TCB(cpu, tcb) READ_MEMORY(cpu, tcb + GUEST_TCB_TID_OFFSET)"
echo "#define STACK_FROM_TCB(tcb) PAGE_ALIGN(tcb)"
echo "#define GUEST_STACK_SIZE (PAGE_SIZE-GUEST_TCB_T_SIZE)"
echo
echo "#define GUEST_TCB_STATE_FLAG_OFFSET 20" # for off_runqueue
echo
echo "#define GUEST_PCB_PID_OFFSET 24" # initial_tid
echo "#define PID_FROM_PCB(cpu, pcb) READ_MEMORY(cpu, pcb + GUEST_PCB_PID_OFFSET)"

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
	if echo "$LAST_TIMER_INSTR" | grep iret 2>&1 >/dev/null; then
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

TIMER_WRAP_ENTER=`get_func $TIMER_WRAPPER`

echo "#define GUEST_TIMER_WRAP_ENTER     0x$TIMER_WRAP_ENTER"
echo "#define GUEST_TIMER_WRAP_EXIT      0x$TIMER_WRAP_EXIT"

CS_ENTER=`get_func $CONTEXT_SWITCH`
CS_EXIT=`get_func_end $CONTEXT_SWITCH`
echo "#define GUEST_CONTEXT_SWITCH_ENTER 0x$CS_ENTER"
echo "#define GUEST_CONTEXT_SWITCH_EXIT  0x$CS_EXIT"

echo

# TODO - find some way to kill this
echo "#define GUEST_FORK_RETURN_SPOT     0x`get_sym get_to_userspace`"

READLINE_WINDOW=`get_func $READLINE`
READLINE_WINDOW_END=`get_func_end $READLINE`

echo "#define GUEST_READLINE_WINDOW_ENTER 0x$READLINE_WINDOW"
echo "#define GUEST_READLINE_WINDOW_EXIT 0x$READLINE_WINDOW_END"

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

echo "#define GUEST_MUTEX_LOCK 0x`get_func mutex_lock`"
echo "#define GUEST_VANISH 0x`get_func do_vanish`"
echo "#define GUEST_VANISH_END 0x`get_func_end do_vanish`"
echo "#define GUEST_MUTEX_LOCK_MUTEX_ARGNUM 1"

echo "#define GUEST_MUTEX_IGNORES { \\"
ignore_mutexes
echo -e "\t}"

echo

echo "#endif"
