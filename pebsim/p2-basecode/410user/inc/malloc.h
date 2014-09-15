#include <stdlib.h>
#include <types.h>

#ifndef _MALLOC_WRAPPERS_H_
#define _MALLOC_WRAPPERS_H_

void *malloc(size_t size);
void *calloc(size_t nelt, size_t eltsize);
void *realloc(void *buf, size_t new_size);
void free(void *buf);

void *_malloc(size_t size);
void *_calloc(size_t nelt, size_t eltsize);
void *_realloc(void *buf, size_t new_size);
void _free(void *buf);

#endif /* _MALLOC_WRAPPERS_H_ */
