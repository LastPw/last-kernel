#ifndef TYPES_H
#define TYPES_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef signed int     i32;
typedef signed char    s8;
typedef signed char    i8;
typedef signed short   s16;
typedef signed long long s64;
typedef signed long long i64;

#if !defined(_UINTPTR_T_DEFINED) && !defined(_STDINT_H) && !defined(_STDINT_H_) && !defined(_STDINT) && !defined(__DEFINED_uintptr_t)
#define _UINTPTR_T_DEFINED
#define _INTPTR_T_DEFINED
#if defined(__UINTPTR_TYPE__)
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__ intptr_t;
#else
typedef unsigned long uintptr_t;
typedef long intptr_t;
#endif
#endif

/* size_t/ptrdiff_t live in stddef.h (builtin __SIZE_TYPE__/__PTRDIFF_TYPE__,
   both 64-bit on x86_64). */

#endif
