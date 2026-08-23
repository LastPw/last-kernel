#ifndef LINUX_COMPAT_H
#define LINUX_COMPAT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Linux x86_64 Syscall Numbers ── */
#define LINUX_SYS_READ              0u
#define LINUX_SYS_WRITE             1u
#define LINUX_SYS_OPEN              2u
#define LINUX_SYS_CLOSE             3u
#define LINUX_SYS_STAT              4u
#define LINUX_SYS_FSTAT             5u
#define LINUX_SYS_LSTAT             6u
#define LINUX_SYS_POLL              7u
#define LINUX_SYS_LSEEK             8u
#define LINUX_SYS_MMAP              9u
#define LINUX_SYS_MPROTECT         10u
#define LINUX_SYS_MUNMAP           11u
#define LINUX_SYS_BRK              12u
#define LINUX_SYS_RT_SIGACTION     13u
#define LINUX_SYS_RT_SIGPROCMASK   14u
#define LINUX_SYS_RT_SIGRETURN     15u
#define LINUX_SYS_IOCTL            16u
#define LINUX_SYS_PREAD64          17u
#define LINUX_SYS_PWRITE64         18u
#define LINUX_SYS_READV            19u
#define LINUX_SYS_WRITEV           20u
#define LINUX_SYS_ACCESS           21u
#define LINUX_SYS_PIPE             22u
#define LINUX_SYS_SELECT           23u
#define LINUX_SYS_SCHED_YIELD      24u
#define LINUX_SYS_MREMAP           25u
#define LINUX_SYS_DUP              32u
#define LINUX_SYS_DUP2             33u
#define LINUX_SYS_NANOSLEEP        35u
#define LINUX_SYS_GETPID           39u
#define LINUX_SYS_SOCKET           41u
#define LINUX_SYS_CONNECT          42u
#define LINUX_SYS_ACCEPT           43u
#define LINUX_SYS_SENDTO           44u
#define LINUX_SYS_RECVFROM         45u
#define LINUX_SYS_SHUTDOWN         48u
#define LINUX_SYS_BIND             49u
#define LINUX_SYS_LISTEN           50u
#define LINUX_SYS_CLONE            56u
#define LINUX_SYS_FORK             57u
#define LINUX_SYS_VFORK            58u
#define LINUX_SYS_EXECVE           59u
#define LINUX_SYS_EXIT             60u
#define LINUX_SYS_WAIT4            61u
#define LINUX_SYS_KILL             62u
#define LINUX_SYS_UNAME            63u
#define LINUX_SYS_FCNTL            72u
#define LINUX_SYS_FSYNC            74u
#define LINUX_SYS_GETCWD           79u
#define LINUX_SYS_CHDIR            80u
#define LINUX_SYS_MKDIR            83u
#define LINUX_SYS_RMDIR            84u
#define LINUX_SYS_UNLINK           87u
#define LINUX_SYS_GETUID          102u
#define LINUX_SYS_GETGID          104u
#define LINUX_SYS_GETEUID         107u
#define LINUX_SYS_GETEGID         108u
#define LINUX_SYS_ARCH_PRCTL      158u
#define LINUX_SYS_GETTID          186u
#define LINUX_SYS_FUTEX           202u
#define LINUX_SYS_GETDENTS64      217u
#define LINUX_SYS_SET_TID_ADDR    218u
#define LINUX_SYS_CLOCK_GETTIME   228u
#define LINUX_SYS_EXIT_GROUP      231u
#define LINUX_SYS_EPOLL_WAIT      232u
#define LINUX_SYS_EPOLL_CTL       233u
#define LINUX_SYS_OPENAT          257u
#define LINUX_SYS_NEWFSTATAT      262u
#define LINUX_SYS_UNLINKAT        263u
#define LINUX_SYS_EPOLL_CREATE1   291u
#define LINUX_SYS_PIPE2           293u
#define LINUX_SYS_GETRANDOM       318u

/* ── Linux Errno Values (returned as negative) ── */
#define LINUX_EPERM          1
#define LINUX_ENOENT         2
#define LINUX_ESRCH          3
#define LINUX_EINTR          4
#define LINUX_EIO            5
#define LINUX_EBADF          9
#define LINUX_ECHILD        10
#define LINUX_EAGAIN        11
#define LINUX_ENOMEM        12
#define LINUX_EACCES        13
#define LINUX_EFAULT        14
#define LINUX_EBUSY         16
#define LINUX_EEXIST        17
#define LINUX_ENODEV        19
#define LINUX_ENOTDIR       20
#define LINUX_EISDIR        21
#define LINUX_EINVAL        22
#define LINUX_ENFILE        23
#define LINUX_EMFILE        24
#define LINUX_ENOTTY        25
#define LINUX_ENOSPC        28
#define LINUX_ESPIPE        29
#define LINUX_EPIPE         32
#define LINUX_ENOSYS        38
#define LINUX_ENOTEMPTY     39
#define LINUX_ENOTSOCK      88
#define LINUX_ECONNREFUSED 111

/* ── Linux Auxiliary Vector Tags ── */
#define LINUX_AT_NULL        0
#define LINUX_AT_IGNORE      1
#define LINUX_AT_EXECFD      2
#define LINUX_AT_PHDR        3
#define LINUX_AT_PHENT       4
#define LINUX_AT_PHNUM       5
#define LINUX_AT_PAGESZ      6
#define LINUX_AT_BASE        7
#define LINUX_AT_FLAGS       8
#define LINUX_AT_ENTRY       9
#define LINUX_AT_NOTELF     10
#define LINUX_AT_UID        11
#define LINUX_AT_EUID       12
#define LINUX_AT_GID        13
#define LINUX_AT_EGID       14
#define LINUX_AT_PLATFORM   15
#define LINUX_AT_HWCAP      16
#define LINUX_AT_CLKTCK     17
#define LINUX_AT_SECURE     23
#define LINUX_AT_RANDOM     25
#define LINUX_AT_HWCAP2     26
#define LINUX_AT_EXECFN     31
#define LINUX_AT_SYSINFO_EHDR 33

/* ── Linux Kernel Sandboxing Profile ── */
typedef struct {
    u8 active;
    u32 sandbox_id;
    u32 max_memory_bytes;
    u32 allocated_bytes;
    u32 peak_memory_bytes;
    u32 max_fds;
    u32 open_fds;
    u8 seccomp_strict;
    u32 syscall_count;
    u32 violations_blocked;
    char chroot_path[64];
} linux_sandbox_t;

/* ── Linux Compatibility Layer APIs ── */
int linux_compat_init(void);
int linux_sandbox_create(u32 max_mem_mb, u32 max_fds, u8 seccomp_strict, const char* root_path);
void linux_sandbox_destroy(int sandbox_id);
linux_sandbox_t* linux_sandbox_get(int sandbox_id);

i64 linux_syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, int sandbox_id);
int linux_run_binary(const u8* elf_bytes, u32 elf_size, const char* name, int sandbox_id);

#ifdef __cplusplus
}
#endif

#endif /* LINUX_COMPAT_H */
