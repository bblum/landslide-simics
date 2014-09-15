/** @file spec/compiler.h
 *  @brief Useful macros.  Inspired by Linux compiler*.h and kernel.h.
 *  @author bblum S2011
 */

#ifndef _SPEC_COMPILER_H
#define _SPEC_COMPILER_H

/* Function annotations */
#ifndef NORETURN // noreturn might also be defined in spec/syscall.h...
# define NORETURN __attribute__((noreturn))
#endif

# define MUST_CHECK __attribute__((warn_unused_result))

/* Force a compilation error if condition is false, but also produce a result
 * (of value 0 and type size_t), so it can be used e.g. in a structure
 * initializer (or whereever else comma expressions aren't permitted). */
/* Linux calls these BUILD_BUG_ON_ZERO/_NULL, which is rather misleading. */
#define STATIC_ZERO_ASSERT(condition) (sizeof(struct { int:-!(condition); }))
#define STATIC_NULL_ASSERT(condition) ((void *)STATIC_ZERO_ASSERT(condition))

/* Force a compilation error if condition is false */
#define STATIC_ASSERT(condition) ((void)STATIC_ZERO_ASSERT(condition))

#endif /* !_SPEC_COMPILER_H */
