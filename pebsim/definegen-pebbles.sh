#!/bin/sh

# this auto-generates a chunk of the kernel_specifics.c file.
# comments can be found there... (sorry)

KERNEL_IMG=$1

if [ ! -f $KERNEL_IMG ]; then
	echo "invalid kernel image specified"
	exit 1
fi

function get_sym {
	objdump -t $KERNEL_IMG | grep "\<$1\>" | cut -d" " -f1
}

function get_func {
	objdump -d $KERNEL_IMG | grep "<$1>:" | cut -d" " -f1
}

function get_func_end {
	objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | tail -n 2 | head -n 1 | sed 's/ //g' | cut -d":" -f1
}

CURRENT_TCB=`get_sym thr_current`
echo "#define GUEST_CURRENT_TCB 0x$CURRENT_TCB"

echo "#define GUEST_TCB_TID_OFFSET 0"
echo "#define TID_FROM_TCB(ls, tcb) \\"
echo -e "\tSIM_read_phys_memory(ls->cpu0, tcb + GUEST_TCB_TID_OFFSET, WORD_SIZE)"
echo

RQ=`get_sym runqueue`
echo "#define GUEST_RQ_ADDR 0x$RQ"
echo

Q_ADD=`get_func sch_queue_append`
echo "#define GUEST_Q_ADD                0x$Q_ADD"
echo "#define GUEST_Q_ADD_Q_ARGNUM       1"
echo "#define GUEST_Q_ADD_TCB_ARGNUM     2"
Q_REMOVE=`get_func sch_queue_remove`
echo "#define GUEST_Q_REMOVE             0x$Q_REMOVE"
echo "#define GUEST_Q_REMOVE_Q_ARGNUM    1"
echo "#define GUEST_Q_REMOVE_TCB_ARGNUM  2"
Q_POP=`get_func_end sch_queue_pop`
echo "#define GUEST_Q_POP_RETURN         0x$Q_POP"
echo "#define GUEST_Q_POP_Q_ARGNUM       1"

echo

TIMER_WRAP_ENTER=`get_func timer_handler_wrapper`
TIMER_WRAP_EXIT=`get_func_end timer_handler_wrapper`
echo "#define GUEST_TIMER_WRAP_ENTER     0x$TIMER_WRAP_ENTER"
echo "#define GUEST_TIMER_WRAP_EXIT      0x$TIMER_WRAP_EXIT"

CS_ENTER=`get_func context_switch`
CS_EXIT=`get_func_end context_switch`
echo "#define GUEST_CONTEXT_SWITCH_ENTER 0x$CS_ENTER"
echo "#define GUEST_CONTEXT_SWITCH_EXIT  0x$CS_EXIT"

SCHED_INIT_EXIT=`get_func_end sch_init`
echo "#define GUEST_SCHED_INIT_EXIT      0x$SCHED_INIT_EXIT"

SCHED_LOCK=`get_sym scheduler_locked`
echo "#define GUEST_SCHEDULER_LOCK       0x$SCHED_LOCK"
echo "#define GUEST_SCHEDULER_LOCKED(x)  (x)"

echo

function get_fork_window {
	# XXX this is a terrible hack
	MISBEHAVE_MODE=`get_sym misbehave_mode`
	MISBEHAVE_MODE=`echo $MISBEHAVE_MODE | sed 's/^00//'`
	objdump -d $KERNEL_IMG | grep -A10000 "<$1>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep "cmpl.*$MISBEHAVE_MODE" | sed 's/ //g' | cut -d":" -f1
}

FORK_WINDOW=`get_fork_window fork`
THRFORK_WINDOW=`get_fork_window thread_fork`
SLEEP_WINDOW=`objdump -d $KERNEL_IMG | grep -A10000 "<sleep>:" | tail -n+2 | grep -m 1 -B10000 ^$ | grep "call.*sch_runqueue_remove" | sed 's/ //g' | cut -d":" -f1`
VANISH_WINDOW=`objdump -d $KERNEL_IMG | grep -A100000 '<vanish>:' | tail -n+2 | grep -m 1 -B10000 ^$ | grep "call.*cond_wait" | tail -n 1 | sed 's/ //g' | cut -d":" -f1`

echo "#define GUEST_FORK_WINDOW_ENTER    0x$FORK_WINDOW"
echo "#define GUEST_THRFORK_WINDOW_ENTER 0x$THRFORK_WINDOW"
echo "#define GUEST_SLEEP_WINDOW_ENTER   0x$SLEEP_WINDOW"
echo "#define GUEST_VANISH_WINDOW_ENTER  0x$VANISH_WINDOW"



