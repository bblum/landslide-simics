/** @file htm.h
 *  @brief This file defines the interface for transactional memory.
 */

#ifndef __HTM_H
#define __HTM_H

#define _XBEGIN_STARTED    (~0u)
#define _XABORT_EXPLICIT   (1 << 0)
#define _XABORT_RETRY      (1 << 1)
#define _XABORT_CONFLICT   (1 << 2)
#define _XABORT_CAPACITY   (1 << 3)
#define _XABORT_DEBUG      (1 << 4)
#define _XABORT_NESTED     (1 << 5)
#define _XABORT_CODE(x)    (((x) >> 24) & 0xFF)

int _xbegin(void);
void _xend(void);
__attribute__((noreturn)) void _xabort(int code);

#endif /* HTM_H */
