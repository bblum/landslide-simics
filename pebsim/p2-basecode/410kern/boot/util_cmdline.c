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
#include <boot/util.h>
#include <kvmphys.h>
#include <stdlib/ctype.h>
#include <stdlib/stdlib.h>
#include <string/string.h>
#include <malloc/malloc_internal.h>

    /* Note that this system uses _malloc to grab memory before the whole
     * environment is up and running.
     *
     * Requires that malloc_lmm is initialized somehow (see mb_util_lmm).
     */

static char prog_name[] = "kernel";
static char *null_args[2] = {prog_name, 0};

static const char delim[] = " \f\n\r\t\v";

void mb_util_cmdline(mbinfo_t *mbi, int *argc, char ***argv, char ***envp)
{

	if (mbi->flags & MULTIBOOT_CMDLINE)
	{
		char *cl = (char*)phystokv(mbi->cmdline);
		unsigned cllen = strlen(cl);
		char *targ[1 + cllen], *tvar[cllen];
		unsigned narg = 0, nvar = 0;
		char *tok;

		/* Parse out the tokens in the command line.
		   XXX Might be good to handle quotes.  */
		for (tok = strtok(cl, delim); tok; tok = strtok(0, delim))
		{
			if (strchr(tok, '='))
				tvar[nvar++] = tok;
			else
				targ[narg++] = tok;
		}

		/* Copy the pointer arrays to permanent heap memory.  */
		*argv = _malloc(sizeof(char*) * (narg + nvar + 2));
		if (!*argv) panic("No memory to parse command line");

		memcpy(*argv, targ, sizeof(char*) * narg);
		*argv[narg] = 0;
		*argc = narg;

		*envp = &*argv[narg+1];
		memcpy(*envp, tvar, sizeof(char*) * nvar);
		*envp[nvar] = 0;
	}
	else
	{
		/* No command line.  */
		*argc = 1;
		*argv = null_args;
		*envp = null_args + 1;
	}
}

