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

#include <ctype.h>
#include <stdio.h>

/*
 * Print a buffer hexdump style.  Example:
 *
 * .---------------------------------------------------------------------------.
 * | 00000000       2e2f612e 6f757400 5445524d 3d787465       ./a.out.TERM=xte |
 * | 00000010       726d0048 4f4d453d 2f616673 2f63732e       rm.HOME=/afs/cs. |
 * | 00000020       75746168 2e656475 2f686f6d 652f6c6f       utah.edu/home/lo |
 * | 00000030       6d657700 5348454c 4c3d2f62 696e2f74       mew.SHELL=/bin/t |
 * | 00000040       63736800 4c4f474e 414d453d 6c6f6d65       csh.LOGNAME=lome |
 * | 00000050       77005553 45523d6c 6f6d6577 00504154       w.USER=lomew.PAT |
 * | 00000060       483d2f61 66732f63 732e7574 61682e65       H=/afs/cs.utah.e |
 * | 00000070       64752f68 6f6d652f 6c6f6d65 772f6269       du/home/lomew/bi |
 * | 00000080       6e2f4073 79733a2f 6166732f 63732e75       n/@sys:/afs/cs.u |
 * | 00000090       7461682e 6564                             tah.ed           |
 * `---------------------------------------------------------------------------'
 *
 * It might be useful to have an option for printing out little-endianly.
 * Adapted from Godmar's hook.c.
 */
void
hexdump(void *buf, int len)
{
	int i, j;
	char *b = (char *)buf;

	printf(".---------------------------------------------------------------------------.\n");
	for (i = 0; i < len; i += 16) {
		printf("| %08x      ", i);
		for (j = i; j < i+16; j++) {
			if (j % 4 == 0)
				printf(" ");
			if (j >= len)
				printf("  ");
			else
				printf("%02x", (unsigned char)b[j]);
		}

		printf("       ");
		for (j = i; j < i+16; j++)
			if (j >= len)
				printf(" ");
			else
				printf("%c", isgraph(b[j]) ? b[j] : '.');
		printf(" |\n");
	}
	printf("`---------------------------------------------------------------------------'\n");
}
