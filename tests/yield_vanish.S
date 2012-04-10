/************************************************************************//**
 * @file yield_vanish.S
 * @brief A test for yield/vanish races.
 * **> Public: Yes
 * **> Covers: yield, vanish
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
	int $THREAD_FORK_INT
	cmpl $0x0,%eax
	jne parent
	int $VANISH_INT
parent:
	mov %eax,%esi
	int $YIELD_INT # yield(child_tid);
	int $VANISH_INT
