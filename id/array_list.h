/**
 * @file array_list.h
 * @brief polymorphic auto-expanding array list
 * @author Ben Blum
 */

#ifndef __ID_ARRAY_LIST_H
#define __ID_ARRAY_LIST_H

#include <limits.h>

#include "common.h"
#include "xcalls.h"

#define ARRAY_LIST(T) struct {		\
		unsigned int size;	\
		unsigned int capacity;	\
		T *array;		\
	}

#define ARRAY_LIST_INIT(a, initial_capacity) do {				\
		typeof(a) __a = (a);						\
		__a->size = 0;							\
		__a->capacity = (initial_capacity);				\
		/* NB avoid side effects in initial capacity */			\
		__a->array = XMALLOC(__a->capacity, typeof(*__a->array));	\
	} while (0)

#define ARRAY_LIST_FREE(a) FREE((a)->array)

#define ARRAY_LIST_CLONE(dest, src) do {					\
		STATIC_ASSERT(SAME_TYPE(*(dest)->array, *(src)->array));	\
		typeof(dest) __dest = (dest);					\
		typeof(dest) __src  = (src);					\
		__dest->size = __src->size;					\
		/* space optimization for long-term storage */			\
		__dest->capacity = __src->size;					\
		__dest->array = XMALLOC(__src->size, typeof(*__src->array));	\
		memcpy(__dest->array, __src->array,				\
		       __src->size * sizeof(*__src->array));			\
	} while (0)

#define ARRAY_LIST_FOREACH(a, i, p) \
	/* cannot avoid side effects here :( */ \
	for(i = 0, p = &(a)->array[i]; i < (a)->size; i++, p = &(a)->array[i])

/* &a->array[i] with a bounds check */
#define ARRAY_LIST_GET(a, i) ({							\
		unsigned int __i = (i);						\
		typeof(a) __a = (a);						\
		assert(__i < __a->size && "array list index out of bounds");	\
		&__a->array[__i];						\
	})

#define ARRAY_LIST_SIZE(a) ((a)->size)

/* O(1) */
#define ARRAY_LIST_APPEND(a, val) do {					\
		typeof(a) __a = (a);					\
		assert(__a->size <= __a->capacity);			\
		/* grow array if necessary */				\
		if (__a->size == __a->capacity) {			\
			typeof(__a->array) old_array = __a->array;	\
			assert(__a->capacity < UINT_MAX / 2);		\
			__a->capacity++; /* in case of 0 */		\
			__a->capacity *= 2;				\
			__a->array = XMALLOC(__a->capacity,		\
						typeof(*__a->array));	\
			memcpy(__a->array, old_array,			\
			       __a->size * sizeof(*__a->array));	\
			FREE(old_array);				\
		}							\
		__a->array[__a->size] = (val);				\
		__a->size++;						\
	} while (0)
	
/* swap elements at two places */
#define ARRAY_LIST_SWAP(a, i, j) do {					\
		typeof((a)->array) __i_ptr = ARRAY_LIST_GET(a, (i));	\
		typeof((a)->array) __j_ptr = ARRAY_LIST_GET(a, (j));	\
		typeof(*(a)->array) __tmp;				\
		__tmp = *__i_ptr;					\
		*__i_ptr = *__j_ptr;					\
		*__j_ptr = __tmp;					\
	} while (0)

/* O(n) */
#define ARRAY_LIST_REMOVE(a, i) do { \
		unsigned int __i = (i);						\
		typeof(a) __a = (a);						\
		assert(__a->size != 0);						\
		assert(__i < __a->size && "array list index out of bounds");	\
		for (unsigned int __j = __i + 1; __j < __a->size; __j++) {	\
			memcpy(&__a->array[__j - 1], &__a->array[__j],		\
			       sizeof(*__a->array));				\
		}								\
		__a->size--;							\
	} while (0)

/* O(1) but doesn't preserve order */
#define ARRAY_LIST_REMOVE_SWAP(a, i) do { \
		/* __i0 and __a0 because wtf, gcc, regarding shadowing. */ 	\
		unsigned int __i0 = (i);					\
		typeof(a) __a0 = (a);						\
		assert(__a0->size != 0);						\
		ARRAY_LIST_SWAP(__a0, __i0, __a0->size - 1);			\
		__a0->size--;							\
	} while (0)

#endif
