/**
 * @file compiler.h
 * @brief Defines some useful macros that Dave might object to. Inspired by
 *        linux's compiler*.h and kernel.h.
 * @author Ben Blum <bblum@andrew.cmu.edu>
 */

#ifndef __LS_COMPILER_H
#define __LS_COMPILER_H

/* Function annotations */
// XXX: simics's headers need this not to be defined here
// # define NORETURN __attribute__((noreturn))
#define MUST_CHECK __attribute__((warn_unused_result))

#define MAYBE_UNUSED __attribute__((unused))

/* Force a compilation error if condition is false, but also produce a result
 * (of value 0 and type size_t), so it can be used e.g. in a structure
 * initializer (or whereever else comma expressions aren't permitted). */
/* Linux calls these BUILD_BUG_ON_ZERO/_NULL, which is rather misleading. */
#define STATIC_ZERO_ASSERT(condition) (sizeof(struct { int:-!(condition); }))
#define STATIC_NULL_ASSERT(condition) ((void *)STATIC_ZERO_ASSERT(condition))

/* Force a compilation error if condition is false */
#define STATIC_ASSERT(condition) ((void)STATIC_ZERO_ASSERT(condition))

/* Force a compilation error if a constant expression is not a power of 2 */
#define STATIC_ASSERT_POWER_OF_2(n)          \
	STATIC_ASSERT(!((n) == 0 || (((n) & ((n) - 1)) != 0)))

/* Type and array checking */
#define SAME_TYPE(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define MUST_BE_ARRAY(a) STATIC_ZERO_ASSERT(!SAME_TYPE((a), &(a)[0]))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + MUST_BE_ARRAY(arr))

/* MIN()/MAX() implementations that avoid the MAX(x++,y++) problem and provide
 * strict typechecking. */
#if 0 // XXX: simics's headers need this not to be defined here
#define MIN(x, y) ({			\
	typeof(x) _min1 = (x);		\
	typeof(y) _min2 = (y);		\
	(void) (&_min1 == &_min2);	\
	_min1 < _min2 ? _min1 : _min2; })

#define MAX(x, y) ({			\
	typeof(x) _max1 = (x);		\
	typeof(y) _max2 = (y);		\
	(void) (&_max1 == &_max2);	\
	_max1 > _max2 ? _max1 : _max2; })
#endif

#define container_of(ptr, type, member) ({          \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type,member) );})

#define ASSERT_UNSIGNED(addr) do {					\
		typeof(addr) __must_not_be_signed = (typeof(addr))(-1);	\
		assert(__must_not_be_signed > 0 &&			\
		       "type error: unsigned type required here");	\
	} while (0)

#endif /* !_INC_COMPILER_H */
