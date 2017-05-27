/** @file htm.c
 *  @brief This file defines the interface for transactional memory.
 */

#include <htm.h>

__attribute__((noinline)) int _xbegin(void)      { return _XBEGIN_STARTED; }
__attribute__((noinline)) void _xend(void)       { }
__attribute__((noinline,noreturn)) void _xabort(int code) { while (1); }
