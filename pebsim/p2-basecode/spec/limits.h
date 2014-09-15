/** @file 410kern/inc/limits.h
 *  @author elly1 U2009
 *  @brief Type size limits.
 */

#ifndef LIMITS_H
#define LIMITS_H

#define CHAR_BIT        8

#define SCHAR_MIN       (-128)
#define SCHAR_MAX       127

#define UCHAR_MAX       255

#define SHRT_MIN        (-32768)
#define SHRT_MAX        32767

#define USHRT_MAX       65535

#define INT_MIN         ((signed int)0x80000000)
#define INT_MAX         ((signed int)0x7fffffff)

#define UINT_MAX        ((unsigned int)0xffffffff)

#define LONG_MIN        ((signed long)0x80000000L)
#define LONG_MAX        ((signed long)0x7FFFFFFFL)

#define ULONG_MAX       ((unsigned long)0xFFFFFFFFL)

#define LLONG_MIN       ((signed long long)0x8000000000000000LL)
#define LLONG_MAX       ((signed long long)0x7FFFFFFFFFFFFFFFLL)

#define ULLONG_MAX      ((unsigned long long)0xFFFFFFFFFFFFFFFFLL)

#endif /* !LIMITS_H */
