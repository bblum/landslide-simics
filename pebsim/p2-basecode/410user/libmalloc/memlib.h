/*
 * memlib.h
 * Modified for use in 15-410 at CMU
 * Zachary Anderson(zra)
 */

#ifndef _MEMLIB_H
#define _MEMLIB_H

void *mem_init(int size);
void *mem_sbrk(int incr);

#endif /* _MEMLIB_H */
