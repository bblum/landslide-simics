/* This should look a lot like 15-213 support code.
 * Guess what...
 */

/* memlib.c - functions for modeling the memory system */
/* $begin memlib */

#include <stddef.h>
#include <stdio.h>
#include <syscall.h>

/* #define PAGE_SIZE       0x00001000 */
/* #define PAGE_ALIGN_MASK 0xFFFFF000 */
#define PAGE_ALIGN_MASK ((unsigned int) ~((unsigned int) (PAGE_SIZE-1)))

#ifndef NULL
#define NULL 0
#endif

/* private global variables */
static char *mem_max_addr;   /* max virtual address for the heap */
static char *mem_brkp; /* Simulated brk pointer */
static char *mem_alloctop; /* Maximum allocated address */

extern void *_end; /* The end of the ELF binary address space */

/* 
 * mem_init - initializes the memory system model
 */
void mem_init(int max_heap_addr)
{
  /* The max address for the heap. */
  mem_max_addr = (char*)max_heap_addr;
  mem_brkp = (char*)&_end + PAGE_SIZE;
  mem_brkp = (char*)((int)mem_brkp & PAGE_ALIGN_MASK);
  while (new_pages(mem_brkp, PAGE_SIZE))
    mem_brkp += PAGE_SIZE;
  mem_alloctop = mem_brkp + PAGE_SIZE;
}

/* 
 * mem_sbrk - simply uses the the sbrk function. Extends the heap 
 *    by incr bytes and returns the start address of the new area. In
 *    this model, the heap cannot be shrunk.
 */
void *mem_sbrk(int incr) 
{
    char *old_brk = mem_brkp;

    /* Error check the request. */
    if ( (incr < 0) || ((old_brk + incr) > mem_max_addr)) {
      return (void *)NULL;
    }

    if (old_brk + incr > mem_alloctop) {
      int allocincr = old_brk + incr - mem_alloctop;
      allocincr += PAGE_SIZE - 1;
      allocincr &= PAGE_ALIGN_MASK;

      /* Issue a SBRK for more memory. */
      if (new_pages((void*)mem_alloctop, allocincr)) {
	return (void *)NULL;
      }

      mem_alloctop += allocincr;
    }

    mem_brkp += incr;

    return (void *)old_brk;
}
/* $end memlib */
