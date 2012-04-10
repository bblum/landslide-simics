/************************************************************************//**
 * @file double_thread_fork.S
 * @brief A test for thread_fork/vanish races.
 * **> Public: Yes
 * **> Covers: thread_fork, vanish
 * **> NeedsWork: No
 * **> For: P3
 * **> Authors: bblum
 * **> Notes: For landslide
 * @author Ben Blum (bblum)
 * @bug No bugs.
 ****************************************************************************/

#include <syscall_int.h>

.global main

main:
	int $THREAD_FORK_INT # fork 1st thread
	cmpl $0x0,%eax
	je first_child
parent:
	int $VANISH_INT
first_child:
	int $THREAD_FORK_INT # fork 2nd thread
	int $VANISH_INT      # both threads vanish
