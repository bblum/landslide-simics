/** @file lib/stdint.h
 *
 *  @brief Standard integer type definitions
 *
 *  @bug The standard integer state of our library is a little questionable.
 *  I think types.h has become a dumping ground for things that don't fit well,
 *  and some of that should move here/be deleted.
 *  It's missing a few things that it probably should have (wchar, ptrdiff
 *  sizing), but I think it should be fine until students actually use those
 *  features.
 *
 *  @author matthewj S2008
    @author Matt Bryant (mbryant) S'16
 */

#ifndef LIB_STDINT_H
#define LIB_STDINT_H

#ifndef ASSEMBLER

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef unsigned long long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed long int32_t;
typedef signed long long int64_t;

typedef int intptr_t;
typedef unsigned int uintptr_t;

typedef int64_t intmax_t;
typedef uint64_t uintmax_t;

typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;

typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

typedef int8_t int_fast8_t;
typedef int16_t int_fast16_t;
typedef int32_t int_fast32_t;
typedef int64_t int_fast64_t;

typedef uint8_t uint_fast8_t;
typedef uint16_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

#endif /* !ASSEMBLER */

#define UINT8_C(x) (x)
#define UINT16_C(x) (x)
#define UINT32_C(x) (x ## U)
#define UINT64_C(x) (x ## ULL)

#define INT8_C(x) (x)
#define INT16_C(x) (x)
#define INT32_C(x) (x)
#define INT64_C(x) (x ## LL)

#define INTMAX_C(x) (x ## LL)
#define UINTMAX_C(x) (x ## ULL)

#define INT8_MIN       (-128)
#define INT8_MAX       (127)
#define INT16_MIN      (-32768)
#define INT16_MAX      (32767)
#define INT32_MIN      (0x80000000)
#define INT32_MAX      (0x7FFFFFFF)
#define INT64_MIN      (0x8000000000000000LL)
#define INT64_MAX      (0x7FFFFFFFFFFFFFFFLL)

#define UINT8_MAX       (255)
#define UINT16_MAX      (65535)
#define UINT32_MAX      (0xFFFFFFFF)
#define UINT64_MAX      (0xFFFFFFFFFFFFFFFFULL)

#define INT_LEAST8_MIN     INT8_MIN
#define INT_LEAST8_MAX     INT8_MAX
#define INT_LEAST16_MIN    INT16_MIN
#define INT_LEAST16_MAX    INT16_MAX
#define INT_LEAST32_MIN    INT32_MIN
#define INT_LEAST32_MAX    INT32_MAX
#define INT_LEAST64_MIN    INT64_MIN
#define INT_LEAST64_MAX    INT64_MAX

#define UINT_LEAST8_MAX    UINT8_MAX
#define UINT_LEAST16_MAX   UINT16_MAX
#define UINT_LEAST32_MAX   UINT32_MAX
#define UINT_LEAST64_MAX   UINT64_MAX

#define INT_FAST8_MIN      INT8_MIN
#define INT_FAST8_MAX      INT8_MAX
#define INT_FAST16_MIN     INT16_MIN
#define INT_FAST16_MAX     INT16_MAX
#define INT_FAST32_MIN     INT32_MIN
#define INT_FAST32_MAX     INT32_MAX
#define INT_FAST64_MIN     INT64_MIN
#define INT_FAST64_MAX     INT64_MAX

#define UINT_FAST8_MAX     UINT8_MAX
#define UINT_FAST16_MAX    UINT16_MAX
#define UINT_FAST32_MAX    UINT32_MAX
#define UINT_FAST64_MAX    UINT64_MAX

#endif /* !LIB_STDINT_H */
