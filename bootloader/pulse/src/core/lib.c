#include "lib.h"

VOID *memset(VOID *dst, int c, UINTN n)
{
    UINT8 *d = dst;
    while (n--)
        *d++ = (UINT8)c;
    return dst;
}

VOID *memcpy(VOID *dst, const VOID *src, UINTN n)
{
    UINT8 *d = dst;
    const UINT8 *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

VOID *memmove(VOID *dst, const VOID *src, UINTN n)
{
    UINT8 *d = dst;
    const UINT8 *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

UINTN strlen(const char *s)
{
    UINTN n = 0;
    while (s[n])
        n++;
    return n;
}

int strncmp(const char *a, const char *b, UINTN n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int)(UINT8)*a - (int)(UINT8)*b;
}
