/* Tiny freestanding string/memory helpers.
   Built with -ffreestanding (-fno-builtin), so plain loops are safe. */
#ifndef PULSE_LIB_H
#define PULSE_LIB_H

#include "pulsebase.h"

VOID *memset(VOID *dst, int c, UINTN n);
VOID *memcpy(VOID *dst, const VOID *src, UINTN n);
VOID *memmove(VOID *dst, const VOID *src, UINTN n);
UINTN strlen(const char *s);
int   strncmp(const char *a, const char *b, UINTN n);

#endif
