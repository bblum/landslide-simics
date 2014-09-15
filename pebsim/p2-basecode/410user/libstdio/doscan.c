/* 
 * Mach Operating System
 * Copyright (c) 1993 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
 *	File: sscanf.c
 * 	Author: Alessandro Forin, Carnegie Mellon University
 *	Date:	3/93
 *
 *	Parse trivially simple strings
 */

#include <stddef.h>
#include <stdarg.h>
#include <types.h>
#include <ctype.h>
#include "doscan.h"

/*
 * All I need is a miracle ...
 * to keep this from growing like all the other sscanf!
 */
int
_doscan(const char *fmt, va_list vp,
	int (*getc)(void *getc_arg),
	void (*ungetc)(int c, void *getc_arg),
	void *getc_arg)
{
	register	int c;
	boolean_t	neg;
	boolean_t	discard;
	boolean_t invalid;
	int		vals = 0;

	while ((c = *fmt++) != 0) {

	    if (c != '%') {
	        if (isspace(c))
		{
			while (isspace(c = getc(getc_arg)));
			if (c == 0) break;
			ungetc(c, getc_arg);
			continue;
		}
	    	else if (c == getc(getc_arg))
			continue;
		else
			break;	/* mismatch */
	    }

	    discard = 0;
			invalid = 1;

	more_fmt:

			c = getc(getc_arg);
			if (c == 0)
				break; /* end of input */

	    switch (*fmt++) {

	    case 'd':
	    {
		long n = 0;

		neg =  c == '-';
		if (neg) c = getc(getc_arg);

		while (c >= '0' && c <= '9') {
		    n = n * 10 + (c - '0');
		    c = getc(getc_arg);
		    invalid = 0;
		}
		ungetc(c, getc_arg);

		if (neg) n = -n;

		/* done, store it away */
		if (!discard)
		{
			int *p = va_arg(vp, int *);
			*p = n;
		}

	        break;
	    }

	    case 'x':
	    {
		long n = 0;

		neg =  c == '-';
		if (neg) c = getc(getc_arg);

		while (1)
		{
		    if ((c >= '0') && (c <= '9'))
			n = n * 16 + (c - '0');
		    else if ((c >= 'a') && (c <= 'f'))
			n = n * 16 + (c - 'a' + 10);
		    else if ((c >= 'A') && (c <= 'F'))
			n = n * 16 + (c - 'A' + 10);
		    else
			break;
		    c = getc(getc_arg);
				invalid = 0;
		}
		ungetc(c, getc_arg);	/* retract lookahead */

		if (neg) n = -n;

		/* done, store it away */
		if (!discard)
		{
			int *p = va_arg(vp, int *);
			*p = n;
		}

	        break;
	    }

	    case 's':
	    {
		char *buf = NULL;

		if (!discard)
			buf = va_arg(vp, char *);

		while (c && !isspace(c))
		{
		    if (!discard)
		    	*buf++ = c;
		    c = getc(getc_arg);
				invalid = 0;
		}
		ungetc(c, getc_arg);	/* retract lookahead */

		if (!discard)
		    *buf = 0;

		break;
	    }

	    case '*':
	    	discard = 1;
		goto more_fmt;

	    default:
	        break;
	    }

		if (invalid)
			break;
		else if (!discard)
			vals++;
	}

	return vals;
}

