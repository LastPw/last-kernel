#ifndef MUSL_SYS_H
#define MUSL_SYS_H

#include "types.h"

#define SYS_read           0
#define SYS_write          1
#define SYS_open           2
#define SYS_close          3
#define SYS_stat           4
#define SYS_fstat          5
#define SYS_mmap           9
#define SYS_mprotect      10
#define SYS_munmap        11
#define SYS_brk           12
#define SYS_rt_sigaction  13
#define SYS_ioctl         16
#define SYS_getpid        39
#define SYS_socket        41
#define SYS_connect       42
#define SYS_accept        43
#define SYS_sendto        44
#define SYS_recvfrom      45
#define SYS_clone         56
#define SYS_exit          60
#define SYS_futex        202
#define SYS_clock_gettime 228
#define SYS_epoll_create1 291
#define SYS_epoll_ctl     233
#define SYS_epoll_wait    232

int musl_sys_dispatch(u32 sys_nr, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64* out_result);

#endif /* MUSL_SYS_H */
