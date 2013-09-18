/************************************************************************//**
 * @file remove_new_new_pages.S
 * @brief A test for new_pages/remove_pages races.
 * **> Public: Yes
 * **> Covers: thread_fork, vanish, new_pages, remove_pages
 * **> NeedsWork: No
 * **> For: P3
 * **> Authors: bblum
 * **> Notes: For landslide
 * @author Ben Blum (bblum)
 * @bug No bugs.
 ****************************************************************************/

#include <syscall_int.h>

#define ADDR 0x15410000

.global main

main:
	int $THREAD_FORK_INT
	cmpl $0x0,%eax
	je child
parent:
        movl $ADDR,%esi
        int $REMOVE_PAGES_INT
	int $VANISH_INT
child:
	pushl $4096
	pushl $ADDR
	mov %esp,%esi
	int $THREAD_FORK_INT # two threads will new_pages
	int $NEW_PAGES_INT
	int $VANISH_INT
