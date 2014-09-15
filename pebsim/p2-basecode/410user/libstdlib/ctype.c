/*
 * Copyright (c) 1996-1994 The University of Utah and
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

int isascii(int c)
{
	return ((c) >= 0) && ((c) <= 126);
}

int iscntrl(int c)
{
	return ((c) < ' ') || ((c) > 126);
}

int isdigit(int c)
{
	return ((c) >= '0') && ((c) <= '9');
}

int isgraph(int c)
{
	return ((c) > ' ') && ((c) <= 126);
}

int islower(int c)
{
	return (c >= 'a') && (c <= 'z');
}

int isprint(int c)
{
	return ((c) >= ' ') && ((c) <= 126);
}

int isspace(int c)
{
	return ((c) == ' ') || ((c) == '\f')
		|| ((c) == '\n') || ((c) == '\r')
		|| ((c) == '\t') || ((c) == '\v');
}

int isupper(int c)
{
	return (c >= 'A') && (c <= 'Z');
}

int isxdigit(int c)
{
	return isdigit(c) ||
		((c >= 'A') && (c <= 'F')) ||
		((c >= 'a') && (c <= 'f'));
}

int isalpha(int c)
{
	return islower(c) || isupper(c);
}

int isalnum(int c)
{
	return isalpha(c) || isdigit(c);
}

int ispunct(int c)
{
	return isgraph(c) && !isalnum(c);
}

int toupper(int c)
{
	return ((c >= 'a') && (c <= 'z')) ? (c - 'a' + 'A') : c;
}

int tolower(int c)
{
	return ((c >= 'A') && (c <= 'Z')) ? (c - 'A' + 'a') : c;
}
