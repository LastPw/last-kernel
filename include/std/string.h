#ifndef LASTOS_FS_STRING_H
#define LASTOS_FS_STRING_H

#include "stdint.h"
#include "stddef.h"

typedef __SIZE_TYPE__ size_t;

void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int v, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
void* memchr(const void* s, int c, size_t n);
size_t strlen(const char* s);
int   strcmp(const char* s1, const char* s2);
int   strncmp(const char* s1, const char* s2, size_t n);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, size_t n);
char* strcat(char* dst, const char* src);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
char* strdup(const char* s);
int   strcasecmp(const char* s1, const char* s2);
int   strncasecmp(const char* s1, const char* s2, size_t n);

#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

#endif /* LASTOS_FS_STRING_H */
