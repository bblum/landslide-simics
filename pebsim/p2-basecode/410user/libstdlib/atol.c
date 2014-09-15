#include <stddef.h>
#include <stdlib.h>

long
atol(const char *str)
{
	return strtol(str, NULL, 10);
}

