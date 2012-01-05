/**
 * @file kernel_specifics_types.h
 * @brief Guest kernel data types
 * @author Ben Blum (bblum)
 */

#ifndef __LS_KERNEL_SPECIFICS_TYPES_H
#define __LS_KERNEL_SPECIFICS_TYPES_H

#include "compiler.h"

#define KSTACK_NUM_INDICES (4096/WORD_SIZE)

/** @brief Thread Control Block - holds information specific to a thread */
struct guest_tcb {
	int tid;
	/* pointer to the PCB of the process that this thread belongs to */
	void *parent;
	/* each TCB sits on a linkedlist of all threads in a single process */
	struct guest_tcb *thr_next;
	struct guest_tcb *thr_prev;
	/* TCBs also sit on scheduler queues - runnable or descheduled */
	struct guest_tcb *q_next;
	struct guest_tcb *q_prev;
	/* thread-specific runflag, used in cas2i for example */
	int runflag;
	/* the thread's kernel stack - needs to be an array of 32-bit types so
	 * we can manipulate it when setting it up (in fork() for example) */
	int stack[KSTACK_NUM_INDICES];
	/* some things need to be saved on the stack when we context switch.
	 * other things need to be saved elsewhere. */
	uint32_t esp;
	/* Threads may be blocking on condition variables. If so, the TCB will
	 * be used as a nobe on the cvar's waiting list. No code should ever
	 * touch this except cond.c */
	struct guest_tcb *cond_queue_next;
	/* if a thread is blocking on a sleep() call, the timer tick count for
	 * when it will be woken up is stored here. */
	int wakeup_time;
};

#define KERN_TCB_SIZE ((10+KSTACK_NUM_INDICES)*WORD_SIZE)

static inline const char *kern_tcb_field_name(int offset)
{
	switch (offset / WORD_SIZE) {
		case 0: return "tid";
		case 1: return "parent";
		case 2: return "thr_next";
		case 3: return "thr_prev";
		case 4: return "q_next";
		case 5: return "q_prev";
		case 6: return "runflag";
		case 7 ... (7+KSTACK_NUM_INDICES-1): return "stack";
		case (7+KSTACK_NUM_INDICES): return "esp";
		case (8+KSTACK_NUM_INDICES): return "cond_queue_next";
		case (9+KSTACK_NUM_INDICES): return "wakeup_time";
		default: return "((unknown))";
	}
}

struct guest_mutex {
	int locked;
	int owner;
};

/* Condition variable type. This would be in cond.h but it needs to be defined
 * before guest_pcb. */
struct guest_cond {
	/* cvar-specific lock */
	struct guest_mutex mutex;
	/* list of threads blocked on the cvar */
	struct guest_tcb *waiting_head;
	struct guest_tcb *waiting_tail;
};

/** @brief Process Control Block - holds information specific to a process */
struct guest_pcb {
	int pid;
	/* threads is a list of all active threads belonging to this process.
	 * deadlist is a list of TCBs from threads that have vanished. Both
	 * lists are protected by threads_mutex. */
	struct guest_tcb *threads;
	struct guest_tcb *deadlist;
	struct guest_mutex threads_mutex;
	/* children is a list of all running child processes; zombies is a
	 * list of PCBs from vanished processes safe to reap. children_mutex
	 * protects both lists. reaper_cond is the condition variable that
	 * a wait()ing parent thread blocks on.
	 * Note: The reason the children lists need tail pointers whereas the
	 * threadlists don't is because the children lists also need to have
	 * append functionality (in the case of reparenting). */
	struct guest_pcb *children;
	struct guest_pcb *children_tail;
	struct guest_pcb *zombies;
	struct guest_pcb *zombies_tail;
	struct guest_mutex children_mutex;
	struct guest_cond reaper_cond;
	int exit_status; /* to be collected by whoever reaps us */
	/* the PCB will also be on a list with all its siblings */
	struct guest_pcb *child_next;
	struct guest_pcb *child_prev;
	/* a process needs to know who its parent process is */
	struct guest_pcb *parent;
	struct guest_mutex parent_mutex;
	/* where does this process's page directory live */
	uint32_t cr3;
	/* count of how many pages are allocated to this process */
	int page_count;
	/* represents the top of the automatic stack region */
	uint32_t stack_region;
	/* process-specific lock for the pagefault handler */
	struct guest_mutex page_lock;
};

#define KERN_MUTEX_SIZE (2*WORD_SIZE)
#define KERN_COND_SIZE  ((2*WORD_SIZE)+KERN_MUTEX_SIZE)
#define KERN_PCB_SIZE ((14*WORD_SIZE)+(4*KERN_MUTEX_SIZE)+KERN_COND_SIZE)

static inline const char *kern_pcb_field_name(int offset)
{
	static const char *names[] =
		{ "pid", "threads", "deadlist", "threads_mutex.locked",
		  "threads_mutex.owner", "children", "children_tail",
		  "zombies", "zombies_tail", "children_mutex.locked",
		  "children_mutex.owner", "reaper_cond.mutex.owner",
		  "reaper_cond.mutex.locked", "reaper_cond.waiting_head",
		  "reaper_cond.waiting_tail", "exit_status", "child_next",
		  "child_prev", "parent", "parent_mutex.locked",
		  "parent_mutex.owner", "cr3", "page_count", "stack_region",
		  "page_lock.locked", "page_lock.owner" };
	if (offset / WORD_SIZE < ARRAY_SIZE(names))
		return names[offset / WORD_SIZE];
	else
		return "((unknown))";
}

#endif
