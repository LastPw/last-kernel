/* Freestanding override of <fcntl.h> for the BearSSL static-lib build.
   sysrng.c uses O_RDONLY when opening /dev/urandom; the kernel never calls
   this path (it seeds via br_ssl_engine_inject_entropy). */
#ifndef LASTOS_FS_FCNTL_H
#define LASTOS_FS_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

int open(const char* path, int flags);

#endif
