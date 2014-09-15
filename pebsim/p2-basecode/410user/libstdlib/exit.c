/*
 * exit.c
 * For new set_status()/vanish() interface, Spring 2007
 * @author Dave Eckhardt (de0u)
 */

#include <syscall.h>

void set_status(int status);
void vanish(void) NORETURN;

void exit(int status)
{
	set_status(status);
	vanish();
}
