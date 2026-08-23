#ifndef LASTOS_FS_STDLIB_H
#define LASTOS_FS_STDLIB_H

#include "stdint.h"
#include "stddef.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t num, size_t size);
void* realloc(void* ptr, size_t new_size);
void abort(void);
void exit(int status);
int atoi(const char* str);
int abs(int n);
long strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);

void* bsearch(const void* key, const void* base, size_t num, size_t size,
              int (*compar)(const void*, const void*));

#endif /* LASTOS_FS_STDLIB_H */
