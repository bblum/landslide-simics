/* 
 * Copyright (c) 1996 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 */
/*
 * Definitions for the base VM environment provided by the OS toolkit:
 * how to change pointers between physical, virtual, and linear addresses, etc.
 */
#ifndef	_FLUX_X86_BASE_VM_H_
#define _FLUX_X86_BASE_VM_H_

#include <types.h>

/** This variable is expected always to contain
 *  the kernel virtual address at which physical memory is mapped.
 *  It may change as paging is turned on or off.
 *
 * @note Provided by 410kern/entry.c, defaults to 0 as expected of the CS410
 * environment.
 */
extern vm_offset_t phys_mem_va;

/* Calculate a kernel virtual address from a physical address.  */
#define phystokv(pa)	((vm_offset_t)(pa) + phys_mem_va)

/* Same, but in reverse.
   This only works for the region of kernel virtual addresses
   that directly map physical addresses.  */
#define kvtophys(va)	((vm_offset_t)(va) - phys_mem_va)

#endif /* _FLUX_X86_BASE_VM_H_ */
