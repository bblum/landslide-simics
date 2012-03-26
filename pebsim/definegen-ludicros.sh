#!/bin/sh

# this auto-generates a chunk of the kernel_specifics.c file.
# comments can be found there... (sorry)

KERNEL_IMG=$1
KERNEL_NAME=$2

if [ ! -f "$KERNEL_IMG" ]; then
	echo "invalid kernel image specified"
	exit 1
fi

if [ -z "$KERNEL_NAME" ]; then
	echo "what is the name of this kernel?"
	exit 1
fi

KERNEL_NAME_LOWER=`echo $KERNEL_NAME | tr '[:upper:]' '[:lower:]'`
KERNEL_NAME_UPPER=`echo $KERNEL_NAME | tr '[:lower:]' '[:upper:]'`

function get_sym {
	objdump -t $KERNEL_IMG | grep " $1$" | cut -d" " -f1
}

function get_func {
	objdump -d $KERNEL_IMG | grep "<$1>:" | cut -d" " -f1
}

function get_func_end {
	objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | tail -n 2 | head -n 1 | sed 's/ //g' | cut -d":" -f1
}

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

TIMER_WRAP_ENTER=`get_func interrupt_idt_stub_32`
TIMER_WRAP_EXIT=`get_func_end interrupt_dispatch`
echo "#define GUEST_TIMER_WRAP_ENTER     0x$TIMER_WRAP_ENTER"
echo "#define GUEST_TIMER_WRAP_EXIT      0x$TIMER_WRAP_EXIT"

CS_ENTER=`get_func sched_run_next_thread`
CS_EXIT=`get_func_end sched_run_next_thread`
echo "#define GUEST_CONTEXT_SWITCH_ENTER 0x$CS_ENTER"
echo "#define GUEST_CONTEXT_SWITCH_EXIT  0x$CS_EXIT"

echo

# TODO - find some way to kill this
echo "#define GUEST_FORK_RETURN_SPOT     0x`get_sym get_to_userspace`"

READLINE_WINDOW=`get_func sys_readline`
READLINE_WINDOW_END=`get_func_end sys_readline`

echo "#define GUEST_READLINE_WINDOW_ENTER 0x$READLINE_WINDOW"
echo "#define GUEST_READLINE_WINDOW_EXIT 0x$READLINE_WINDOW_END"

echo

######################################
#### Mutexes / Deadlock detection ####
######################################

# No yield mutexes in ludicros.
# BLOCKED_WINDOW=`objdump -d $KERNEL_IMG | grep -A10000 "<mutex_lock>:" | grep -m 1 "call.*<yield>" | sed 's/ //g' | cut -d":" -f1`
# echo "#define GUEST_MUTEX_LOCK_ENTER   0x`get_func mutex_lock`"
echo "#define GUEST_MUTEX_LOCK_MUTEX_ARGNUM 1"
# echo "#define GUEST_MUTEX_BLOCKED      0x$BLOCKED_WINDOW"
# echo "#define GUEST_MUTEX_LOCK_EXIT    0x`get_func_end mutex_lock`"
# echo "#define GUEST_MUTEX_UNLOCK_ENTER 0x`get_func mutex_unlock`"
# echo "#define GUEST_MUTEX_UNLOCK_MUTEX_ARGNUM 1"
# echo "#define GUEST_MUTEX_UNLOCK_EXIT  0x`get_func_end mutex_unlock`"

echo

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

function ignore_func {
	echo -e "\t{ 0x`get_func $1`, 0x`get_func_end $1` }, \\"
}

function ignore_sym {
	echo -e "\t{ 0x`get_sym $1`, $2 }, \\"
}

# Things that are likely to always touch shared memory that you don't care about
# such as runqueue links, but not those that the globals list filters out, such
# as links inside each tcb.
echo "#define GUEST_SCHEDULER_FUNCTIONS { \\"
ignore_func sched_find_blocked_thread
ignore_func sched_find_thread
ignore_func sched_block
ignore_func sched_unblock
ignore_func sched_process_wakeups
ignore_func sched_run_next_thread
ignore_func sched_switch_context_and_reschedule
ignore_func sched_do_context_switch
ignore_func context_return
ignore_func sched_reschedule
ignore_func sched_yield
echo -e "\t}"

INT_SIZE=4
PTR_SIZE=4
MUTEX_SIZE=`echo $(($INT_SIZE+$INT_SIZE))`
Q_SIZE=`echo $(($PTR_SIZE+$PTR_SIZE))`
COND_SIZE=`echo $(($INT_SIZE+$MUTEX_SIZE+$Q_SIZE))`
SEM_SIZE=`echo $(($INT_SIZE+$INT_SIZE+$INT_SIZE+$MUTEX_SIZE+$COND_SIZE))`

echo "#define GUEST_SCHEDULER_GLOBALS { \\"
ignore_sym ticks $INT_SIZE
ignore_sym lock_count $INT_SIZE
ignore_sym unlock_int_flag $INT_SIZE
ignore_sym sched_run_list $Q_SIZE
ignore_sym sched_block_list $Q_SIZE
ignore_sym sched_sleep_list $Q_SIZE
ignore_sym sched_run_list_lock $MUTEX_SIZE
ignore_sym sched_block_list_lock $MUTEX_SIZE
ignore_sym sched_sleep_list_lock $MUTEX_SIZE
echo -e "\t}"

echo

#######################
#### Choice points ####
#######################

echo "#define GUEST_MUTEX_LOCK 0x`get_func mutex_lock`"
echo "#define GUEST_VANISH 0x`get_func do_vanish`"
echo "#define GUEST_VANISH_END 0x`get_func_end do_vanish`"

echo "#define GUEST_MUTEX_IGNORES { \\"
ignore_sym frame_lock $MUTEX_SIZE
ignore_sym zfod_sibling_lock $MUTEX_SIZE
ignore_sym pm_promise_lock $MUTEX_SIZE
ignore_sym mm_lock $SEM_SIZE
ignore_sym kb_input_sem $SEM_SIZE
ignore_sym kb_char_sem $SEM_SIZE
echo -e "\t}"

echo

echo "#endif"
