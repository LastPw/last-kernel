#include "string.h"

u32 str_len(const char* s) {
    u32 n = 0;
    if (!s) return 0;
    while (s[n]) {
        n++;
    }
    return n;
}

i32 str_cmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) {
            return (i32)((u8)*a - (u8)*b);
        }
        a++;
        b++;
    }
    return (i32)((u8)*a - (u8)*b);
}

i32 str_ncmp(const char* a, const char* b, u32 n) {
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (i32)((u8)a[i] - (u8)b[i]);
        }
    }
    return 0;
}

void str_cpy(char* dst, const char* src) {
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

void str_ncpy(char* dst, const char* src, u32 n) {
    u32 i;
    if (!dst || n == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i < n - 1 && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void str_cat(char* dst, const char* src) {
    dst += str_len(dst);
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';
}

/* SECURITY: bounded concatenation — prevents buffer overflows when
 * building strings from untrusted components. dst_size is the total
 * capacity of the destination buffer including the NUL terminator. */
void str_ncat(char* dst, const char* src, u32 dst_size) {
    if (!dst || !src || dst_size == 0u) return;
    u32 dst_len = str_len(dst);
    if (dst_len >= dst_size) return;
    u32 remaining = dst_size - dst_len - 1u;
    u32 i = 0u;
    while (src[i] && i < remaining) {
        dst[dst_len + i] = src[i];
        i++;
    }
    dst[dst_len + i] = '\0';
}

/* OpenBSD/Linux standard strlcpy: safely copy bounded string with guaranteed NUL termination */
u32 str_lcpy(char* dst, const char* src, u32 size) {
    if (!src) {
        if (dst && size > 0) dst[0] = '\0';
        return 0;
    }
    u32 srclen = str_len(src);
    if (!dst || size == 0) return srclen;

    u32 copy_len = (srclen >= size) ? (size - 1u) : srclen;
    for (u32 i = 0; i < copy_len; i++) {
        dst[i] = src[i];
    }
    dst[copy_len] = '\0';
    return srclen;
}

/* OpenBSD/Linux standard strlcat: safely append bounded string with guaranteed NUL termination */
u32 str_lcat(char* dst, const char* src, u32 size) {
    if (!dst || size == 0) {
        return src ? str_len(src) : 0;
    }
    if (!src) {
        return str_len(dst);
    }
    u32 dlen = str_len(dst);
    u32 slen = str_len(src);

    if (dlen >= size) {
        return size + slen;
    }

    u32 copy_len = size - dlen - 1u;
    if (slen < copy_len) {
        copy_len = slen;
    }

    for (u32 i = 0; i < copy_len; i++) {
        dst[dlen + i] = src[i];
    }
    dst[dlen + copy_len] = '\0';
    return dlen + slen;
}

void* memcpy(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void* memset(void* dst, int v, size_t n) {
    u8* d = (u8*)dst;
    u8 b = (u8)(v & 0xFF);
    for (size_t i = 0; i < n; i++) {
        d[i] = b;
    }
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0u; i--) {
            d[i - 1u] = s[i - 1u];
        }
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return (int)x[i] - (int)y[i];
        }
    }
    return 0;
}

size_t strlen(const char* s) {
    return (size_t)str_len(s);
}

char* str_str(const char* haystack, const char* needle) {
    if (!haystack || !needle) return 0;
    if (!needle[0]) return (char*)haystack;

    u32 hlen = str_len(haystack);
    u32 nlen = str_len(needle);
    if (nlen > hlen) return 0;

    for (u32 i = 0; i <= hlen - nlen; i++) {
        u8 match = 1;
        for (u32 j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) {
                match = 0;
                break;
            }
        }
        if (match) return (char*)(haystack + i);
    }
    return 0;
}

char* strstr(const char* haystack, const char* needle) {
    return str_str(haystack, needle);
}

void* bsearch(const void* key, const void* base, unsigned long num, unsigned long size,
              int (*compar)(const void*, const void*)) {
    unsigned long start = 0;
    unsigned long end = num;
    while (start < end) {
        unsigned long mid = start + (end - start) / 2;
        const void* item = (const char*)base + mid * size;
        int res = compar(key, item);
        if (res == 0) return (void*)item;
        if (res < 0) end = mid;
        else start = mid + 1;
    }
    return 0;
}

static inline char to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

int strcasecmp(const char* s1, const char* s2) {
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (*s1 && *s2) {
        char c1 = to_lower_char(*s1);
        char c2 = to_lower_char(*s2);
        if (c1 != c2) return (int)((unsigned char)c1 - (unsigned char)c2);
        s1++; s2++;
    }
    return (int)((unsigned char)to_lower_char(*s1) - (unsigned char)to_lower_char(*s2));
}

int strncasecmp(const char* s1, const char* s2, unsigned long n) {
    if (n == 0) return 0;
    if (!s1 || !s2) return (s1 == s2) ? 0 : (s1 ? 1 : -1);
    while (n-- > 0) {
        char c1 = to_lower_char(*s1);
        char c2 = to_lower_char(*s2);
        if (c1 != c2) return (int)((unsigned char)c1 - (unsigned char)c2);
        if (*s1 == '\0') break;
        s1++; s2++;
    }
    return 0;
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (base == 0) {
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            base = 16;
            str += 2;
        } else if (str[0] == '0') {
            base = 8;
            str++;
        } else {
            base = 10;
        }
    } else if (base == 16 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    unsigned long res = 0;
    while (*str) {
        int v = -1;
        if (*str >= '0' && *str <= '9') v = *str - '0';
        else if (*str >= 'a' && *str <= 'z') v = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') v = *str - 'A' + 10;
        if (v < 0 || v >= base) break;
        res = res * base + v;
        str++;
    }
    if (endptr) *endptr = (char*)str;
    return res;
}

long strtol(const char* str, char** endptr, int base) {
    if (!str) return 0;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    int sign = 1;
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }
    return sign * (long)strtoul(str, endptr, base);
}

void* memchr(const void* s, int c, unsigned long n) {
    const unsigned char* p = (const unsigned char*)s;
    unsigned char uc = (unsigned char)c;
    while (n--) {
        if (*p == uc) return (void*)p;
        p++;
    }
    return 0;
}

char* strrchr(const char* s, int c) {
    if (!s) return 0;
    const char* last = 0;
    char ch = (char)c;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char*)s;
    return (char*)last;
}
