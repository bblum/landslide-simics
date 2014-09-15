/** @file lib/types.h
 *  @brief Defines some C standard types as well as
 *         wrapper types for some of the imported FLUX library code.
 *  @author matthewj S2008
 */

#ifndef LIB_TYPES_H
#define LIB_TYPES_H

typedef unsigned int size_t;
typedef unsigned long ptrdiff_t;

/* WRAPPERS */

typedef unsigned long vm_offset_t;
typedef unsigned long vm_size_t;

typedef enum {
    FALSE = 0,
    TRUE
} boolean_t;

#endif /* !LIB_TYPES_H */
