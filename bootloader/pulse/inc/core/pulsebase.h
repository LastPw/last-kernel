/* Freestanding base types shared by everything (firmware glue + core). */
#ifndef PULSE_BASE_H
#define PULSE_BASE_H

typedef unsigned long long UINT64;
typedef long long          INT64;
typedef unsigned int       UINT32;
typedef int                INT32;
typedef unsigned short     UINT16;
typedef short              INT16;
typedef unsigned char      UINT8;
typedef signed char        INT8;
typedef unsigned short     CHAR16;
typedef char               CHAR8;
typedef void               VOID;

/* Pointer-width: 8 bytes on x64, 4 on IA-32. */
#if defined(__x86_64__) || defined(_M_X64)
typedef unsigned long long UINTN;
#else
typedef unsigned int       UINTN;
#endif

#ifndef NULL
#define NULL ((VOID *)0)
#endif

#endif
