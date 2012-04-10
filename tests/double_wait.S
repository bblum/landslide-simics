/************************************************************************//**
 * @file wait_vanish.S
 * @brief A test for multiple-waiter races.
 * **> Public: Yes
 * **> Covers: wait, vanish
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
	int $FORK_INT
	cmpl $0x0,%eax
	jne parent
	int $VANISH_INT
parent:
	int $THREAD_FORK_INT
	mov $0x0,%esi
	int $WAIT_INT # two parent threads do wait(NULL);
	int $VANISH_INT
