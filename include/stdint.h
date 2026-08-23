#ifndef LASTOS_STDINT_H
#define LASTOS_STDINT_H
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;
typedef signed long long intmax_t;
typedef unsigned long long uintmax_t;
typedef unsigned long uintptr_t;
typedef signed long intptr_t;
typedef signed char int_fast8_t;
typedef unsigned char uint_fast8_t;
typedef signed short int_fast16_t;
typedef unsigned short uint_fast16_t;
typedef signed int int_fast32_t;
typedef unsigned int uint_fast32_t;
typedef signed long long int_fast64_t;
typedef unsigned long long uint_fast64_t;
typedef signed char int_least8_t;
typedef unsigned char uint_least8_t;
typedef signed short int_least16_t;
typedef unsigned short uint_least16_t;
typedef signed int int_least32_t;
typedef unsigned int uint_least32_t;
typedef signed long long int_least64_t;
typedef unsigned long long uint_least64_t;
#define UINT8_MAX 255u
#define UINT16_MAX 65535u
#define UINT32_MAX 0xFFFFFFFFu
#define INT8_MIN (-128)
#define INT8_MAX 127
#define INT16_MIN (-32768)
#define INT16_MAX 32767
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647
#define INT64_MIN (-9223372036854775807LL - 1LL)
#define INT64_MAX 9223372036854775807LL
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#define SIZE_MAX (~((unsigned long)0))
#define INTPTR_MAX 9223372036854775807L
#define UINTPTR_MAX 0xFFFFFFFFFFFFFFFFUL
#define PTRDIFF_MAX 9223372036854775807L
#define UINT64_C(c) (c ## ULL)
#define INT64_C(c) (c ## LL)
#define UINT32_C(c) (c ## U)
#define INT32_C(c) (c)
#endif
