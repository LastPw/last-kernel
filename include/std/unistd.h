/* Freestanding override of <unistd.h> for the BearSSL static-lib build.
   Declares the fd primitives sysrng.c references (never called by the kernel
   path that seeds via br_ssl_engine_inject_entropy). */
#ifndef LASTOS_FS_UNISTD_H
#define LASTOS_FS_UNISTD_H

typedef long ssize_t;
typedef __SIZE_TYPE__ size_t;
typedef long intptr_t;

int    open(const char* path, int flags);
ssize_t read(int fd, void* buf, size_t n);
ssize_t write(int fd, const void* buf, size_t n);
int    close(int fd);

#endif
