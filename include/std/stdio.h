#ifndef LASTOS_FS_STDIO_H
#define LASTOS_FS_STDIO_H

#include "stdint.h"
#include "stddef.h"
#include "stdarg.h"

typedef void FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char* fmt, ...);
int fprintf(FILE* stream, const char* fmt, ...);
int sprintf(char* str, const char* fmt, ...);
int snprintf(char* str, size_t size, const char* fmt, ...);
int vsnprintf(char* str, size_t size, const char* fmt, va_list ap);
int fputc(int c, FILE *stream);
int putchar(int c);
int fputs(const char *s, FILE *stream);
int puts(const char *s);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);

#endif /* LASTOS_FS_STDIO_H */
