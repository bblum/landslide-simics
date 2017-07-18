#!/bin/bash

# this auto-generates a chunk of the kernel_specifics.c file.
# comments can be found there... (sorry)

##########################
#### helper functions ####
##########################

source ./getfunc.sh

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

WITHIN_KERN_FUNCTIONS=
# The list will appear in order that they are called.
# Later ones specified take precedence (as per the implementation in kernel_specifics.c).
function within_function {
	WITHIN_KERN_FUNCTIONS="${WITHIN_KERN_FUNCTIONS}\\\\\n\t{ 0x`get_func $1`, 0x`get_func_end $1`, 1 },"
}
function without_function {
	WITHIN_KERN_FUNCTIONS="${WITHIN_KERN_FUNCTIONS}\\\\\n\t{ 0x`get_func $1`, 0x`get_func_end $1`, 0 },"
}

WITHIN_USER_FUNCTIONS=
function within_user_function {
	WITHIN_USER_FUNCTIONS="${WITHIN_USER_FUNCTIONS}\\\\\n\t{ 0x`get_user_func $1`, 0x`get_user_func_end $1`, 1 },"
}
function without_user_function {
	WITHIN_USER_FUNCTIONS="${WITHIN_USER_FUNCTIONS}\\\\\n\t{ 0x`get_user_func $1`, 0x`get_user_func_end $1`, 0 },"
}

IGNORE_DR_FUNCTIONS=
function ignore_dr_function {
	USERSPACE_DR_FN=$2
	if [ "$USERSPACE_DR_FN" = 0 ]; then
		# kernel
		# the 0 parameter is unused (for now; there could be a whitelist later)
		IGNORE_DR_FUNCTIONS="${IGNORE_DR_FUNCTIONS}\\\\\n\t{ 0x`get_func $1`, 0x`get_func_end $1`, 0 },"
	else
		# user -- default, including no arg
		IGNORE_DR_FUNCTIONS="${IGNORE_DR_FUNCTIONS}\\\\\n\t{ 0x`get_user_func $1`, 0x`get_user_func_end $1`, 0 },"
	fi
}

DATA_RACE_INFO=
function data_race {
	# 1 = racing eip value
	# 2 = current TID
	# 3 = eip of last "call" instruction
	# 4 = most_recent_syscall
	if [ -z "$1" -o -z "$2" -o -z "$3" -o -z "$4" ]; then
		die "data_race needs four args: got \"$1\" and \"$2\" and \"$3\" and \"$4\""
	fi
	DATA_RACE_INFO="${DATA_RACE_INFO} { $1, $2, $3, $4 },"
}

DISK_IO_FNS=
function disk_io_func {
	if [ -z "$1" ]; then
		die "disk_io_func needs an arg: got \"$1\""
	fi
	DISK_IO_FNS="${DISK_IO_FNS} { 0x`get_func $1`, 0x`get_func_end $1` },"
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
SHELL_TID=
CONTEXT_SWITCH_RETURN=
CONTEXT_SWITCH_2=
EXEC=
YIELD=
SFREE=
MEMSET=
TESTING_USERSPACE=0
PREEMPT_ENABLE_FLAG=
PAGE_FAULT_WRAPPER=
VM_USER_COPY=
VM_USER_COPY_TAIL=
THREAD_KILLED_FUNC=
THREAD_KILLED_ARG_VAL=
PATHOS_SYSCALL_IRET_DISTANCE=
PDE_PTE_POISON=
PRINT_DATA_RACES=0
VERBOSE=0
EXTRA_VERBOSE=0
TABULAR_TRACE=0
ALLOW_LOCK_HANDOFF=0
ICB=0
ICB_START_BOUND=1
OBFUSCATED_KERNEL=0
BUG_ON_THREADS_WEDGED=1
PINTOS_KERNEL=
PINTOS_USERPROG=
ALLOW_REENTRANT_MALLOC_FREE=0
FILTER_DRS_BY_LAST_CALL=0
FILTER_DRS_BY_TID=1 # Note unusual default
TESTING_MUTEXES=0
DR_PPS_RESPECT_WITHIN_FUNCTIONS=0
PREEMPT_EVERYWHERE=0
PURE_HAPPENS_BEFORE=0
HTM=0
HTM_ABORT_CODES=0
source $CONFIG

source ./symbols.sh

########################################
#### Reading config from ID wrapper ####
########################################

ID_WRAPPER_MAGIC=
function id_magic {
	if [ ! -z "$ID_WRAPPER_MAGIC" ]; then
		die "id_magic called more than once; oldval $ID_WRAPPER_MAGIC, newval $1"
	fi
	ID_WRAPPER_MAGIC=$1
}

if [ ! -z "$QUICKSAND_CONFIG_STATIC" ]; then
	if [ ! -f "$QUICKSAND_CONFIG_STATIC" ]; then
		die "Where's $QUICKSAND_CONFIG_STATIC?"
	fi

	# static config will only provide test case, verbose, and magic cookie
	# see build.sh for how dynamic config is handled
	source "$QUICKSAND_CONFIG_STATIC"

	# we can't read PP config from dynamic file, but we at least must find
	# output pipe, so we can echo to it in case we die (see above).
	# XXX: gross
	if [ -f "$QUICKSAND_CONFIG_DYNAMIC" ]; then
		OUTPUT_PIPE=`grep "output_pipe" "$QUICKSAND_CONFIG_DYNAMIC" | cut -d' ' -f2`
	fi
fi

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
if [ ! -z "$QUICKSAND_CONFIG_STATIC" ]; then
	echo " * Iterative deepening md5sum `md5sum $QUICKSAND_CONFIG_STATIC`"
else
	echo " * Iterative deepening md5sum NONE"
fi
echo " * @author Ben Blum <bblum@andrew.cmu.edu>"
echo " */"
echo
echo "#ifndef __LS_KERNEL_SPECIFICS_${KERNEL_NAME_UPPER}_H"
echo "#define __LS_KERNEL_SPECIFICS_${KERNEL_NAME_UPPER}_H"
echo

if [ ! -z "$PINTOS_KERNEL" ]; then
	# Many functionalities in Landslide will change depending on this.
	# For Pebbles, many #defines are expected and Landslide will not compile
	# without them. In Pintos we are not able to provide these whatsoever.
	echo "#define PINTOS_KERNEL"
fi

########################
#### TCB management ####
########################

if [ -z "$PINTOS_KERNEL" ]; then
	# FIXME - deferred to post-userprog to implement this
	echo "#define GUEST_ESP0_ADDR (0x`get_sym $INIT_TSS` + 4)" # see 410kern/x86/asm.S
fi

###################################
#### Thread lifecycle tracking ####
###################################

echo

if [ ! -z "$TIMER_WRAPPER_DISPATCH" ]; then
	# Difficult case, but handled.
	TIMER_WRAP_EXIT=`get_func_ret $TIMER_WRAPPER_DISPATCH`
else
	# check the end instruction for being ret, and spit out a "ask ben for help" warning
	LAST_TIMER_INSTR=`objdump -d $KERNEL_IMG | grep -A10000 "<$TIMER_WRAPPER>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep -v ":.*90.*nop$" | grep -v "xchg.*%ax.*%ax" | tail -n 2 | head -n 1`
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

# XXX: this is not as general as it should be..
echo "#define GUEST_CONTEXT_SWITCH_ENTER 0x`get_func $CONTEXT_SWITCH`"
echo "#define GUEST_CONTEXT_SWITCH_EXIT  0x`get_func_ret $CONTEXT_SWITCH`"
if [ ! -z "$CONTEXT_SWITCH_RETURN" ]; then
	echo "#define GUEST_CONTEXT_SWITCH_EXIT0 0x`get_func_ret $CONTEXT_SWITCH_RETURN`"
fi

if [ ! -z "$CONTEXT_SWITCH_2" ]; then
	echo "#define GUEST_CONTEXT_SWITCH_ENTER2 0x`get_func $CONTEXT_SWITCH2`"
	echo "#define GUEST_CONTEXT_SWITCH_EXIT2  0x`get_func_ret $CONTEXT_SWITCH2`"
fi

echo

# Readline
if [ -z "$PINTOS_KERNEL" ]; then
	echo "#define GUEST_READLINE_WINDOW_ENTER 0x`get_func $READLINE`"
	echo "#define GUEST_READLINE_WINDOW_EXIT 0x`get_func_ret $READLINE`"
fi

# Exec, only required if testing userspace
if [ ! -z "$EXEC" ]; then
	echo "#define GUEST_EXEC_ENTER 0x`get_func $EXEC`"
fi

if [ ! -z "$YIELD" ]; then
	echo "#define GUEST_YIELD_ENTER 0x`get_func $YIELD`"
	echo "#define GUEST_YIELD_EXIT  0x`get_func_ret $YIELD`"
fi

# Don't poison freed blocks
if [ ! -z "$SFREE" ]; then
	if [ ! -z "$MEMSET" ]; then
		POISON_FREE_CALL_ADDR=`objdump -d "$KERNEL_IMG" | grep -A10000 "<$SFREE>:" | grep -m 1 -B10000 "^$" | grep -m 1 "call.*<$MEMSET>" | cut -d":" -f1 | sed 's/ //g'`
		if [ ! -z "$POISON_FREE_CALL_ADDR" ]; then
			echo "#define POISON_FREE_CALL_ADDR 0x$POISON_FREE_CALL_ADDR"
		fi
	fi
fi

echo

# XXX HACK. pathos-only, for userspace data race optimization.
# This is way less general than it could be.
if [ "$OBFUSCATED_KERNEL" = 1 ]; then
	function pathos_syscall {
		ADDR=`get_func $2`
		if [ ! -z "$ADDR" ]; then
			echo "#define PATHOS_${1}_ENTER 0x$ADDR"
		else
			die "$2 missing in pathos"
		fi
		ADDR=`get_func_ret $2`
		if [ ! -z "$ADDR" ]; then
			echo "#define PATHOS_${1}_EXIT 0x$ADDR"
		else
			die "$2 seems noreturn in pathos?"
		fi
	}

	pathos_syscall DESCH wpucGHGqlcxjMT
	pathos_syscall WAIT bIGnbhGy
	pathos_syscall PRINT KXBFbHfAm
	pathos_syscall GCP aXJMTsOgxhAQSNySVq
	pathos_syscall RF IQNTOABvZgFO
fi

###################################
#### Dynamic memory allocation ####
###################################

echo "#define GUEST_LMM_ALLOC_ENTER      0x`get_func $LMM_ALLOC`"
echo "#define GUEST_LMM_ALLOC_EXIT       0x`get_func_ret $LMM_ALLOC`"
echo "#define GUEST_LMM_ALLOC_SIZE_ARGNUM $LMM_ALLOC_SIZE_ARGNUM"
if [ ! -z "$LMM_ALLOC_GEN" ]; then
	echo "#define GUEST_LMM_ALLOC_GEN_ENTER  0x`get_func $LMM_ALLOC_GEN`"
	echo "#define GUEST_LMM_ALLOC_GEN_EXIT   0x`get_func_ret $LMM_ALLOC_GEN`"
	echo "#define GUEST_LMM_ALLOC_GEN_SIZE_ARGNUM $LMM_ALLOC_GEN_SIZE_ARGNUM"
fi
echo "#define GUEST_LMM_FREE_ENTER       0x`get_func $LMM_FREE`"
echo "#define GUEST_LMM_FREE_EXIT        0x`get_func_ret $LMM_FREE`"
echo "#define GUEST_LMM_FREE_BASE_ARGNUM $LMM_FREE_BASE_ARGNUM"
echo "#define GUEST_LMM_INIT_ENTER 0x`get_func $LMM_INIT`"
echo "#define GUEST_LMM_INIT_EXIT 0x`get_func_ret $LMM_INIT`"

# There's another allocator in pintos. We gotta model both of em.
if [ ! -z "$PINTOS_KERNEL" ]; then
	## NB. The pintos page allocator init() doesn't touch any memory in
	## the heap itself; it only initializes bitmaps stored in .data.
	# echo "#define GUEST_PALLOC_INIT_ENTER 0x`get_func palloc_init`"
	# echo "#define GUEST_PALLOC_INIT_EXIT 0x`get_func_ret palloc_init`"

	# palloc_get_page just wraps palloc_get_multiple
	echo "#define GUEST_PALLOC_ALLOC_ENTER 0x`get_func palloc_get_multiple`"
	echo "#define GUEST_PALLOC_ALLOC_EXIT 0x`get_func_ret palloc_get_multiple`"
	echo "#define GUEST_PALLOC_ALLOC_SIZE_ARGNUM 2"
	echo "#define GUEST_PALLOC_ALLOC_SIZE_FACTOR 4096"

	# palloc_free_page just wraps palloc_free_multiple
	echo "#define GUEST_PALLOC_FREE_ENTER 0x`get_func palloc_free_multiple`"
	echo "#define GUEST_PALLOC_FREE_EXIT 0x`get_func_ret palloc_free_multiple`"
	echo "#define GUEST_PALLOC_FREE_BASE_ARGNUM 1"
fi

if [ "$ALLOW_REENTRANT_MALLOC_FREE" = "1" ]; then
	echo "#define ALLOW_REENTRANT_MALLOC_FREE"
else
	# Nothing. (This is an ifdef/ifndef over in the c code.)
	# (Yes, I know the conventions are all over the place. It's a one-person
	# ph.d. project and not worth cleaning up. Gimme a break.)
	echo -n
fi

if [ "$FILTER_DRS_BY_LAST_CALL" = "1" ]; then
	echo "#define FILTER_DRS_BY_LAST_CALL"
fi

if [ "$FILTER_DRS_BY_TID" = "1" ]; then
	echo "#define FILTER_DRS_BY_TID"
fi

if [ "$TESTING_MUTEXES" = "1" ]; then
	echo "#define TESTING_MUTEXES"
fi

if [ "$DR_PPS_RESPECT_WITHIN_FUNCTIONS" = "1" ]; then
	echo "#define DR_PPS_RESPECT_WITHIN_FUNCTIONS"
fi

if [ "$PREEMPT_EVERYWHERE" = "1" ]; then
	echo "#define PREEMPT_EVERYWHERE"
fi

if [ "$PURE_HAPPENS_BEFORE" = "1" ]; then
	echo "#define PURE_HAPPENS_BEFORE"
fi

if [ "$HTM" = "1" ]; then
	echo "#define HTM"
	echo "#define HTM_XBEGIN     0x`get_user_func     _xbegin`"
	echo "#define HTM_XBEGIN_END 0x`get_user_func_end _xbegin`"
	echo "#define HTM_XEND       0x`get_user_func     _xend`"
	echo "#define HTM_XABORT     0x`get_user_func     _xabort`"
	if [ "$HTM_ABORT_CODES" = "1" ]; then
		echo "#define HTM_ABORT_CODES"
	fi
fi

echo

#############################################
#### Kernel image regions / misc symbols ####
#############################################

echo "#define GUEST_IMG_END 0x`get_sym _end`"
echo "#define GUEST_DATA_START 0x`get_sym .data`"
echo "#define GUEST_DATA_END 0x`get_sym .bss`"
echo "#define GUEST_BSS_START 0x`get_sym .bss`"
echo "#define GUEST_BSS_END GUEST_IMG_END"

echo "#define GUEST_PANIC 0x`get_func $KERN_PANIC`"
echo "#define GUEST_KERNEL_MAIN 0x`get_func $KERN_MAIN`"
echo "#define GUEST_START 0x`get_func $KERN_START`"

if [ ! -z "$KERN_HLT" ]; then
	# Potentially pathos-only...
	echo "#define GUEST_HLT_EXIT 0x`get_func_ret $KERN_HLT`"
fi

if [ ! -z "$PAGE_FAULT_WRAPPER" ];  then
	echo "#define GUEST_PF_HANDLER 0x`get_sym $PAGE_FAULT_WRAPPER`"
fi

if [ ! -z "$THREAD_KILLED_FUNC" ]; then
	echo "#define GUEST_THREAD_KILLED 0x`get_func $THREAD_KILLED_FUNC`"
	if [ ! -z "$THREAD_KILLED_ARG_VAL" ]; then
		echo "#define GUEST_THREAD_KILLED_ARG $THREAD_KILLED_ARG_VAL"
	fi
fi

if [ ! -z "$VM_USER_COPY" ];  then
	echo "#define GUEST_VM_USER_COPY_ENTER 0x`get_func $VM_USER_COPY`"
	if [ ! -z "$VM_USER_COPY_TAIL" ]; then
		echo "#define GUEST_VM_USER_COPY_EXIT  0x`get_func_end $VM_USER_COPY_TAIL`"
	else
		echo "#define GUEST_VM_USER_COPY_EXIT  0x`get_func_end $VM_USER_COPY`"
	fi
fi

if [ ! -z "$PATHOS_SYSCALL_IRET_DISTANCE" ]; then
	echo "#define PATHOS_SYSCALL_IRET_DISTANCE $PATHOS_SYSCALL_IRET_DISTANCE"
fi

if [ ! -z "$PDE_PTE_POISON" ]; then
	echo "#define PDE_PTE_POISON $PDE_PTE_POISON"
fi

if [ ! -z "$PINTOS_KERNEL" ]; then
	# The pintos boot sequence contains a very... shall we say...
	# landslide-unfriendly way of calibrating the timer. We'll skip dis.
	echo "#define GUEST_TIMER_CALIBRATE 0x`get_func timer_calibrate`"
	echo "#define GUEST_TIMER_CALIBRATE_END 0x`get_func_end timer_calibrate`"
	echo "#define GUEST_TIMER_CALIBRATE_RESULT 0x`get_sym loops_per_tick`"
	# Obtained via experiment. Independent of landslide or host CPU rate.
	echo "#define GUEST_TIMER_CALIBRATE_VALUE 39270400"
	# For speeding up ide_init and uhci_init.
	echo "#define GUEST_TIMER_MSLEEP 0x`get_func timer_msleep`"
	# Debug/logging.
	echo "#define GUEST_PRINTF 0x`get_func printf`"
	echo "#define GUEST_DBG_PANIC 0x`get_func debug_panic`"
	# For test lifecycle.
	echo "#define GUEST_RUN_TASK_ENTER 0x`get_func run_task`"
	echo "#define GUEST_RUN_TASK_EXIT 0x`get_func_end run_task`"
	# In pebbles, CONSOLE_MEM_BASE is below .text. In pintos, it's above.
	echo "#define GUEST_VGA_CONSOLE_BASE 0xc00b8000"
	echo "#define GUEST_VGA_CONSOLE_END (0xc00b8000 + (80*25*2))"
	echo "#define GUEST_CLI_ENTER 0x`get_func intr_disable`"
	echo "#define GUEST_CLI_EXIT  0x`get_func_end intr_disable`"
	echo "#define GUEST_STI_ENTER 0x`get_func intr_enable`" # intr_set_level calls intr_enable
	echo "#define GUEST_STI_EXIT  0x`get_func_end intr_enable`" # intr_set_level calls intr_enable
	echo "#define GUEST_TIMER_SPLEEP_ENTER 0x`get_func timer_sleep`"
	echo "#define GUEST_TIMER_SPLEEP_EXIT  0x`get_func_end timer_sleep`"
	echo "#define GUEST_LIST_INSERT_ORDERED_EXIT 0x`get_func_end list_insert_ordered`"
fi

echo

############################################################
#### Userspace locations of importance (for p2 testing) ####
############################################################

function define_user_addr {
	# this function may not emit an address if the function is
	# not present in the user binary.
	ADDR=`get_user_func $2`
	if [ ! -z "$ADDR" ]; then
		echo "#define ${1}_ENTER 0x$ADDR"
	fi
	# some functions e.g. panic() don't return; don't emit their ret addr.
	ADDR=`get_user_func_ret $2`
	if [ ! -z "$ADDR" ]; then
		echo "#define ${1}_EXIT 0x$ADDR"
	fi
}
function define_user_sym {
	# this function may not emit an address if the function is
	# not present in the user binary.
	ADDR=`get_user_sym $2`
	if [ ! -z "$ADDR" ]; then
		echo "#define $1 0x$ADDR"
	fi
}

# user malloc

define_user_addr USER_MM_INIT mm_init
define_user_addr USER_MM_MALLOC mm_malloc
define_user_addr USER_MM_FREE mm_free
define_user_addr USER_MM_REALLOC mm_realloc
define_user_addr USER_LOCKED_MALLOC malloc
define_user_addr USER_LOCKED_FREE free
define_user_addr USER_LOCKED_CALLOC calloc
define_user_addr USER_LOCKED_REALLOC realloc
define_user_addr USER_PANIC panic

# user elf regions

define_user_sym USER_IMG_END _end
define_user_sym USER_DATA_START .data
define_user_sym USER_DATA_END _edata
define_user_sym USER_BSS_START __bss_start

# user thread library locations

define_user_addr USER_THR_INIT thr_init
define_user_addr USER_THR_CREATE thr_create
define_user_addr USER_THR_JOIN thr_join
define_user_addr USER_THR_EXIT thr_exit
define_user_addr USER_MUTEX_INIT mutex_init
define_user_addr USER_MUTEX_LOCK mutex_lock
define_user_addr USER_MUTEX_TRYLOCK mutex_trylock # not necessary; try some different
define_user_addr USER_MUTEX_TRY_LOCK mutex_try_lock # plausible names hoping to find it
define_user_addr USER_MUTEX_UNLOCK mutex_unlock
define_user_addr USER_MUTEX_DESTROY mutex_destroy
define_user_addr USER_COND_WAIT cond_wait
define_user_addr USER_COND_SIGNAL cond_signal
define_user_addr USER_COND_BROADCAST cond_broadcast
define_user_addr USER_SEM_WAIT sem_wait
define_user_addr USER_SEM_SIGNAL sem_signal
define_user_addr USER_RWLOCK_LOCK rwlock_lock
define_user_addr USER_RWLOCK_UNLOCK rwlock_unlock

# user other

echo "#define USER_REPORT_END_FAIL_VAL 1"
define_user_addr USER_REPORT_END report_end
define_user_addr USER_YIELD yield
define_user_addr USER_MAKE_RUNNABLE make_runnable
define_user_addr USER_SLEEP sleep

echo

###############################
#### In-kernel annotations ####
###############################

echo "#define TELL_LANDSLIDE_DECIDE 0x`get_func $TL_DECIDE`"
echo "#define TELL_LANDSLIDE_THREAD_SWITCH 0x`get_func $TL_SWITCH`"
echo "#define TELL_LANDSLIDE_SCHED_INIT_DONE 0x`get_func $TL_INIT_DONE`"
echo "#define TELL_LANDSLIDE_FORKING 0x`get_func $TL_FORKING`"
echo "#define TELL_LANDSLIDE_VANISHING 0x`get_func $TL_VANISH`"
echo "#define TELL_LANDSLIDE_SLEEPING 0x`get_func $TL_SLEEP`"
echo "#define TELL_LANDSLIDE_THREAD_RUNNABLE 0x`get_func $TL_ON_RQ`"
echo "#define TELL_LANDSLIDE_THREAD_DESCHEDULING 0x`get_func $TL_OFF_RQ`"
echo "#define TELL_LANDSLIDE_MUTEX_LOCKING 0x`get_func $TL_MX_LOCK`"
echo "#define TELL_LANDSLIDE_MUTEX_BLOCKING 0x`get_func $TL_MX_BLOCK`"
echo "#define TELL_LANDSLIDE_MUTEX_LOCKING_DONE 0x`get_func $TL_MX_LOCK_DONE`"
echo "#define TELL_LANDSLIDE_MUTEX_UNLOCKING 0x`get_func $TL_MX_UNLOCK`"
echo "#define TELL_LANDSLIDE_MUTEX_UNLOCKING_DONE 0x`get_func $TL_MX_UNLOCK_DONE`"
echo "#define TELL_LANDSLIDE_MUTEX_TRYLOCKING 0x`get_func $TL_MX_TRYLOCK`"
echo "#define TELL_LANDSLIDE_MUTEX_TRYLOCKING_DONE 0x`get_func $TL_MX_TRYLOCK_DONE`"
echo "#define TELL_LANDSLIDE_DUMP_STACK 0x`get_func $TL_STACK`"

if [ ! -z "$PINTOS_KERNEL" ]; then
	# For pintos we'll hook the mutex implementation by name rather than
	# relying on annotations. There's too much conflict across student
	# implementations for any set of annotations to patch cleanly.
	# mutex_init -- check for value!=1 to omit lockset checking.
	echo "#define GUEST_SEMA_INIT_ENTER 0x`get_func sema_init`"
	echo "#define GUEST_SEMA_INIT_SEMA_ARGNUM 1"
	echo "#define GUEST_SEMA_INIT_VALUE_ARGNUM 2"
	# mutex_lock
	echo "#define GUEST_SEMA_DOWN_ENTER 0x`get_func sema_down`"
	echo "#define GUEST_SEMA_DOWN_EXIT 0x`get_func_end sema_down`"
	echo "#define GUEST_SEMA_DOWN_ARGNUM 1"
	# mutex_trylock
	echo "#define GUEST_SEMA_TRY_DOWN_ENTER 0x`get_func sema_try_down`"
	echo "#define GUEST_SEMA_TRY_DOWN_EXIT 0x`get_func_end sema_try_down`"
	echo "#define GUEST_SEMA_TRY_DOWN_ARGNUM 1"
	echo "#define GUEST_SEMA_TRY_DOWN_FAILURE 0" # false
	# mutex_unlock
	echo "#define GUEST_SEMA_UP_ENTER 0x`get_func sema_up`"
	echo "#define GUEST_SEMA_UP_EXIT 0x`get_func_end sema_up`"
	echo "#define GUEST_SEMA_UP_ARGNUM 1"
fi

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
if [ ! -z "$SHELL_TID" ]; then
	echo "#define GUEST_SHELL_TID $SHELL_TID"
fi
echo "#define GUEST_FIRST_TID $FIRST_TID"
if [ -z "$IDLE_TID" ]; then
	echo "#ifdef GUEST_IDLE_TID"
	echo "#undef GUEST_IDLE_TID"
	echo "#endif"
else
	echo "#define GUEST_IDLE_TID $IDLE_TID"
fi

echo "#define GUEST_STARTING_THREAD_CODE do {$STARTING_THREADS } while (0)"

# The config options that student.c once served to provide.

echo "#define CURRENT_THREAD_LIVES_ON_RQ $CURRENT_THREAD_LIVES_ON_RQ"

if [ ! -z "$PREEMPT_ENABLE_FLAG" ]; then
	echo "#define PREEMPT_ENABLE_FLAG 0x`get_sym $PREEMPT_ENABLE_FLAG`"
	echo "#define PREEMPT_ENABLE_VALUE $PREEMPT_ENABLE_VALUE"
fi

###########################
#### User-config stuff ####
###########################

echo -e "$EXTRA_SYMS"

#############################
#### Misc config options ####
#############################

# supports statically defined pps in config.landslide for standalone use;
# if called from quicksand, these will be prepended to any dynamic pps.
echo -e "#define KERN_WITHIN_FUNCTIONS { $WITHIN_KERN_FUNCTIONS }"
echo -e "#define USER_WITHIN_FUNCTIONS { $WITHIN_USER_FUNCTIONS }"
echo -e "#define IGNORE_DR_FUNCTIONS   { $IGNORE_DR_FUNCTIONS   }"

echo "#define DATA_RACE_INFO { $DATA_RACE_INFO }"
echo "#define DISK_IO_FNS { $DISK_IO_FNS }"

if [ "$BUG_ON_THREADS_WEDGED" = "0" -a ! -z "$DISK_IO_FNS" ]; then
	msg "Warning: disk_io_fn has no effect when BUG_ON_THREADS_WEDGED=0."
fi

echo "#define BUG_ON_THREADS_WEDGED $BUG_ON_THREADS_WEDGED"
echo "#define EXPLORE_BACKWARDS $EXPLORE_BACKWARDS"
echo "#define DECISION_INFO_ONLY $DONT_EXPLORE"
echo "#define BREAK_ON_BUG $BREAK_ON_BUG"
echo "#define TESTING_USERSPACE $TESTING_USERSPACE"
echo "#define PRINT_DATA_RACES $PRINT_DATA_RACES"
echo "#define VERBOSE $VERBOSE"
echo "#define EXTRA_VERBOSE $EXTRA_VERBOSE"
echo "#define TABULAR_TRACE $TABULAR_TRACE"
echo "#define ALLOW_LOCK_HANDOFF $ALLOW_LOCK_HANDOFF"
if [ "$ICB" = 1 ]; then
	echo "#define ICB"
	echo "#define ICB_START_BOUND $ICB_START_BOUND"
fi

if [ ! -z "$ID_WRAPPER_MAGIC" ]; then
	echo "#define ID_WRAPPER_MAGIC $ID_WRAPPER_MAGIC"
fi

echo

echo "#endif"
