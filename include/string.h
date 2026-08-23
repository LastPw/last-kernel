#ifndef STRING_H
#define STRING_H

#include "types.h"
#include "stddef.h"

u32 str_len(const char* s);
size_t strlen(const char* s);
i32 str_cmp(const char* a, const char* b);
i32 str_ncmp(const char* a, const char* b, u32 n);
void str_cpy(char* dst, const char* src);
void str_ncpy(char* dst, const char* src, u32 n);
void str_cat(char* dst, const char* src);
void str_ncat(char* dst, const char* src, u32 dst_size);
u32 str_lcpy(char* dst, const char* src, u32 size);
u32 str_lcat(char* dst, const char* src, u32 size);
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int v, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
char* str_str(const char* haystack, const char* needle);
char* strstr(const char* haystack, const char* needle);

#endif
