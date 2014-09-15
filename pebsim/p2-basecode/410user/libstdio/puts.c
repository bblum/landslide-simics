/** @file 410user/libstdio/puts.c
 *  @author elly1 S2010
 *  @brief The puts() libc function
 */

#include <stdio.h>
#include <string.h>
#include <syscall.h>

int puts(const char *s) {
	print(strlen(s), (char *)s);
	putchar('\n');
	return 0;
}
