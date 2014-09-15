#include <stdlib.h>
#include <types.h>

#ifndef _MALLOC_WRAPPERS_H_
#define _MALLOC_WRAPPERS_H_

void *malloc(size_t size);
void *memalign(size_t alignment, size_t size);
void *calloc(size_t nelt, size_t eltsize);
void *realloc(void *buf, size_t new_size);
void free(void *buf);
void *smalloc(size_t size);
void *smemalign(size_t alignment, size_t size);
void sfree(void *buf, size_t size);

#endif /* _MALLOC_WRAPPERS_H_ */
