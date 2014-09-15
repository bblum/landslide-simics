/*
 * Copyright (c) 1994 The University of Utah and
 * the Computer Systems Laboratory (CSL).  All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 */

#include <string.h>
#include <stdlib.h>

char *strstr(const char *haystack, const char *needle)
{
	int hlen = strlen(haystack);
	int nlen = strlen(needle);

	while (hlen >= nlen)
	{
		if (!memcmp(haystack, needle, nlen))
			return (void*)haystack;

		haystack++;
		hlen--;
	}
	return 0;
}

