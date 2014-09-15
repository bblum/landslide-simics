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

#include <boot/multiboot.h>
#include <string/string.h>
#include <malloc/malloc_internal.h>
#include <kvmphys.h>
#include <lmm/lmm_types.h>

/* The 410 world does not care about memory in this much detail.  If we
 * ever start caring this much, these definitions should move back
 * to their own header file so that other users of LMM can see them
 * too.
 * 
 * At the momemnt, though, given only malloc, these are somewhat silly.
 */

/*
 * <1MB memory is most precious, then <16MB memory, then high memory.
 * Assign priorities to each region accordingly
 * so that high memory will be used first when possible,
 * then 16MB memory, then 1MB memory.
 */
#define LMM_PRI_1MB     -2
#define LMM_PRI_16MB    -1
#define LMM_PRI_HIGH    0

/*
 * For memory <1MB, both LMMF_1MB and LMMF_16MB will be set.
 * For memory from 1MB to 16MB, only LMMF_16MB will be set.
 * For all memory higher than that, neither will be set.
 */
#define LMMF_1MB    0x01
#define LMMF_16MB   0x02

static struct lmm_region reg1mb, reg16mb, reghigh;

#define skip(hole_min, hole_max)					\
	if ((max > (hole_min)) && (min < (hole_max)))			\
	{								\
		if (min < (hole_min)) max = (hole_min);			\
		else { min = (hole_max); goto retry; }			\
	}

void mb_util_lmm (mbinfo_t *mbi, lmm_t *lmm)
{
	vm_offset_t min;
	extern char _start[], end[];

	/* Memory regions to skip.  */
	vm_offset_t cmdline_start_pa = mbi->flags & MULTIBOOT_CMDLINE
		? mbi->cmdline : 0;
	vm_offset_t cmdline_end_pa = cmdline_start_pa
		? cmdline_start_pa+strlen((char*)phystokv(cmdline_start_pa))+1
		: 0;

	/* Initialize the base memory allocator
	   according to the PC's physical memory regions.  */
	lmm_init(lmm);

    /* Do the x86 init dance to build our initial regions */
    lmm_add_region(&malloc_lmm, &reg1mb,
            (void*)phystokv(0x00000000), 0x00100000,
            LMMF_1MB | LMMF_16MB, LMM_PRI_1MB);
    lmm_add_region(&malloc_lmm, &reg16mb,
            (void*)phystokv(0x00100000), 0x00f00000,
            LMMF_16MB, LMM_PRI_16MB);
    lmm_add_region(&malloc_lmm, &reghigh,
            (void*)phystokv(0x01000000), 0xfeffffff,
            0, LMM_PRI_HIGH);

	/* Add to the free list all the memory the boot loader told us about,
	   carefully avoiding the areas occupied by boot information.
	   as well as our own executable code, data, and bss.
	   Start at the end of the BIOS data area.  */
	min = 0x500;
	do
	{
		vm_offset_t max = 0xffffffff;

		/* Skip the I/O and ROM area.  */
		skip(mbi->mem_lower * 1024, 0x100000);

		/* Stop at the end of upper memory.  */
		skip(0x100000 + mbi->mem_upper * 1024, 0xffffffff);

		/* Skip our own text, data, and bss.  */
		skip(kvtophys(_start), kvtophys(end));

		/* FIXME: temporary state of affairs */
		extern char __kimg_start[];
		skip(kvtophys(__kimg_start), kvtophys(end));

		/* Skip the important stuff the bootloader passed to us.  */
		skip(cmdline_start_pa, cmdline_end_pa);
		if ((mbi->flags & MULTIBOOT_MODS)
		    && (mbi->mods_count > 0))
		{
			struct multiboot_module *m = (struct multiboot_module*)
				phystokv(mbi->mods_addr);
			unsigned i;

			skip(mbi->mods_addr,
			     mbi->mods_addr +
			     mbi->mods_count * sizeof(*m));
			for (i = 0; i < mbi->mods_count; i++)
			{
				if (m[i].string != 0)
				{
					char *s = (char*)phystokv(m[i].string);
					unsigned len = strlen(s);
					skip(m[i].string, m[i].string+len+1);
				}
				skip(m[i].mod_start, m[i].mod_end);
			}
		}

		/* We actually found a contiguous memory block
		   that doesn't conflict with anything else!  Whew!
		   Add it to the free list.  */
		lmm_add_free(&malloc_lmm, (void *) min, max - min);

		/* Continue searching just past the end of this region.  */
		min = max;

		/* The skip() macro jumps to this label
		   to restart with a different (higher) min address.  */
		retry:;
	}
	while (min < 0xffffffff);
}

