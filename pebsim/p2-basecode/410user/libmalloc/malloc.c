/* The 15-410 C Library
 * malloc.c
 * 
 * Zachary Anderson(zra)
 */

#include "mm_malloc.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h> /* for bzero */

/*
 * Has the mm_malloc library been initialized yet?
 */
static unsigned int inited = 0;


/*
 * wrapper around the mm_malloc library mm_malloc
 */
void *
_malloc( size_t __size )
{
	if( !inited ) {
		if ( mm_init() < 0 ) {
			return NULL;
		}
		inited = 1;
	}
	return mm_malloc( __size );
}

/*
 * calloc implimented with mm_malloc library
 */
void *
_calloc( size_t __nelt, size_t __eltsize )
{
	void *new;

	if( !inited ) {
		if ( mm_init() < 0 ) {
			return NULL;
		}
		inited = 1;
	}

	new = mm_malloc( __nelt * __eltsize );
	if( !new ) {
		return NULL;
	}
	bzero( new, __nelt * __eltsize );
	return new;
}

/*
 * wrapper around the mm_malloc library mm_realloc
 */
void *
_realloc( void *__buf, size_t __new_size )
{
        if( !inited ) {
		/* have to malloc before realloc =P */
		return NULL;
        }
	return mm_realloc( __buf, __new_size );
}

/*
 * wrapper around the mm_malloc library mm_free
 */
void 
_free( void *__buf )
{
	if( !inited ) {
		/* hmm. not really anything to free. */
		return;
	}
	mm_free( __buf );
}
