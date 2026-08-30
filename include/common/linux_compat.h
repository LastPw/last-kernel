#ifndef LINUX_COMPAT_H
#define LINUX_COMPAT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Linux x86_64 Full Syscall Numbers (0 to 455) ── */
#define LINUX_SYS_READ                    0u
#define LINUX_SYS_WRITE                   1u
#define LINUX_SYS_OPEN                    2u
#define LINUX_SYS_CLOSE                   3u
#define LINUX_SYS_STAT                    4u
#define LINUX_SYS_FSTAT                   5u
#define LINUX_SYS_LSTAT                   6u
#define LINUX_SYS_POLL                    7u
#define LINUX_SYS_LSEEK                   8u
#define LINUX_SYS_MMAP                    9u
#define LINUX_SYS_MPROTECT               10u
#define LINUX_SYS_MUNMAP                 11u
#define LINUX_SYS_BRK                    12u
#define LINUX_SYS_RT_SIGACTION           13u
#define LINUX_SYS_RT_SIGPROCMASK         14u
#define LINUX_SYS_RT_SIGRETURN           15u
#define LINUX_SYS_IOCTL                  16u
#define LINUX_SYS_PREAD64                17u
#define LINUX_SYS_PWRITE64               18u
#define LINUX_SYS_READV                  19u
#define LINUX_SYS_WRITEV                 20u
#define LINUX_SYS_ACCESS                 21u
#define LINUX_SYS_PIPE                   22u
#define LINUX_SYS_SELECT                 23u
#define LINUX_SYS_SCHED_YIELD            24u
#define LINUX_SYS_MREMAP                 25u
#define LINUX_SYS_MSYNC                  26u
#define LINUX_SYS_MINCORE                27u
#define LINUX_SYS_MADVISE                28u
#define LINUX_SYS_SHMGET                 29u
#define LINUX_SYS_SHMAT                  30u
#define LINUX_SYS_SHMCTL                 31u
#define LINUX_SYS_DUP                    32u
#define LINUX_SYS_DUP2                   33u
#define LINUX_SYS_PAUSE                  34u
#define LINUX_SYS_NANOSLEEP              35u
#define LINUX_SYS_GETITIMER              36u
#define LINUX_SYS_ALARM                  37u
#define LINUX_SYS_SETITIMER              38u
#define LINUX_SYS_GETPID                 39u
#define LINUX_SYS_SENDFILE               40u
#define LINUX_SYS_SOCKET                 41u
#define LINUX_SYS_CONNECT                42u
#define LINUX_SYS_ACCEPT                 43u
#define LINUX_SYS_SENDTO                 44u
#define LINUX_SYS_RECVFROM               45u
#define LINUX_SYS_SENDMSG                46u
#define LINUX_SYS_RECVMSG                47u
#define LINUX_SYS_SHUTDOWN               48u
#define LINUX_SYS_BIND                   49u
#define LINUX_SYS_LISTEN                 50u
#define LINUX_SYS_GETSOCKNAME            51u
#define LINUX_SYS_GETPEERNAME            52u
#define LINUX_SYS_SOCKETPAIR             53u
#define LINUX_SYS_SETSOCKOPT             54u
#define LINUX_SYS_GETSOCKOPT             55u
#define LINUX_SYS_CLONE                  56u
#define LINUX_SYS_FORK                   57u
#define LINUX_SYS_VFORK                  58u
#define LINUX_SYS_EXECVE                 59u
#define LINUX_SYS_EXIT                   60u
#define LINUX_SYS_WAIT4                  61u
#define LINUX_SYS_KILL                   62u
#define LINUX_SYS_UNAME                  63u
#define LINUX_SYS_SEMGET                 64u
#define LINUX_SYS_SEMOP                  65u
#define LINUX_SYS_SEMCTL                 66u
#define LINUX_SYS_SHMDT                  67u
#define LINUX_SYS_MSGGET                 68u
#define LINUX_SYS_MSGSND                 69u
#define LINUX_SYS_MSGRCV                 70u
#define LINUX_SYS_MSGCTL                 71u
#define LINUX_SYS_FCNTL                  72u
#define LINUX_SYS_FLOCK                  73u
#define LINUX_SYS_FSYNC                  74u
#define LINUX_SYS_FDATASYNC              75u
#define LINUX_SYS_TRUNCATE               76u
#define LINUX_SYS_FTRUNCATE              77u
#define LINUX_SYS_GETDENTS               78u
#define LINUX_SYS_GETCWD                 79u
#define LINUX_SYS_CHDIR                  80u
#define LINUX_SYS_FCHDIR                 81u
#define LINUX_SYS_RENAME                 82u
#define LINUX_SYS_MKDIR                  83u
#define LINUX_SYS_RMDIR                  84u
#define LINUX_SYS_CREAT                  85u
#define LINUX_SYS_LINK                   86u
#define LINUX_SYS_UNLINK                 87u
#define LINUX_SYS_SYMLINK                88u
#define LINUX_SYS_READLINK               89u
#define LINUX_SYS_CHMOD                  90u
#define LINUX_SYS_FCHMOD                 91u
#define LINUX_SYS_CHOWN                  92u
#define LINUX_SYS_FCHOWN                 93u
#define LINUX_SYS_LCHOWN                 94u
#define LINUX_SYS_UMASK                  95u
#define LINUX_SYS_GETTIMEOFDAY           96u
#define LINUX_SYS_GETRLIMIT              97u
#define LINUX_SYS_GETRUSAGE              98u
#define LINUX_SYS_SYSINFO                99u
#define LINUX_SYS_TIMES                 100u
#define LINUX_SYS_PTRACE                101u
#define LINUX_SYS_GETUID                102u
#define LINUX_SYS_SYSLOG                103u
#define LINUX_SYS_GETGID                104u
#define LINUX_SYS_SETUID                105u
#define LINUX_SYS_SETGID                106u
#define LINUX_SYS_GETEUID               107u
#define LINUX_SYS_GETEGID               108u
#define LINUX_SYS_SETPGID               109u
#define LINUX_SYS_GETPPID               110u
#define LINUX_SYS_GETPGRP               111u
#define LINUX_SYS_SETSID                112u
#define LINUX_SYS_SETREUID              113u
#define LINUX_SYS_SETREGID              114u
#define LINUX_SYS_GETGROUPS             115u
#define LINUX_SYS_SETGROUPS             116u
#define LINUX_SYS_SETRESUID             117u
#define LINUX_SYS_GETRESUID             118u
#define LINUX_SYS_SETRESGID             119u
#define LINUX_SYS_GETRESGID             120u
#define LINUX_SYS_GETPGID               121u
#define LINUX_SYS_SETFSUID              122u
#define LINUX_SYS_SETFSGID              123u
#define LINUX_SYS_GETSID                124u
#define LINUX_SYS_CAPGET                125u
#define LINUX_SYS_CAPSET                126u
#define LINUX_SYS_RT_SIGPENDING         127u
#define LINUX_SYS_RT_SIGTIMEDWAIT       128u
#define LINUX_SYS_RT_SIGQUEUEINFO       129u
#define LINUX_SYS_RT_SIGSUSPEND         130u
#define LINUX_SYS_SIGALTSTACK           131u
#define LINUX_SYS_UTIME                 132u
#define LINUX_SYS_MKNOD                 133u
#define LINUX_SYS_USELIB                134u
#define LINUX_SYS_PERSONALITY           135u
#define LINUX_SYS_USTAT                 136u
#define LINUX_SYS_STATFS                137u
#define LINUX_SYS_FSTATFS               138u
#define LINUX_SYS_SYSFS                 139u
#define LINUX_SYS_GETPRIORITY           140u
#define LINUX_SYS_SETPRIORITY           141u
#define LINUX_SYS_SCHED_SETPARAM        142u
#define LINUX_SYS_SCHED_GETPARAM        143u
#define LINUX_SYS_SCHED_SETSCHEDULER    144u
#define LINUX_SYS_SCHED_GETSCHEDULER    145u
#define LINUX_SYS_SCHED_GET_PRIORITY_MAX 146u
#define LINUX_SYS_SCHED_GET_PRIORITY_MIN 147u
#define LINUX_SYS_SCHED_RR_GET_INTERVAL 148u
#define LINUX_SYS_MLOCK                 149u
#define LINUX_SYS_MUNLOCK               150u
#define LINUX_SYS_MLOCKALL              151u
#define LINUX_SYS_MUNLOCKALL            152u
#define LINUX_SYS_VHANGUP               153u
#define LINUX_SYS_MODIFY_LDT            154u
#define LINUX_SYS_PIVOT_ROOT            155u
#define LINUX_SYS__SYSCTL               156u
#define LINUX_SYS_PRCTL                 157u
#define LINUX_SYS_ARCH_PRCTL            158u
#define LINUX_SYS_ADJTIMEX              159u
#define LINUX_SYS_SETRLIMIT             160u
#define LINUX_SYS_CHROOT                161u
#define LINUX_SYS_SYNC                  162u
#define LINUX_SYS_ACCT                  163u
#define LINUX_SYS_SETTIMEOFDAY          164u
#define LINUX_SYS_MOUNT                 165u
#define LINUX_SYS_UMOUNT2               166u
#define LINUX_SYS_SWAPON                167u
#define LINUX_SYS_SWAPOFF               168u
#define LINUX_SYS_REBOOT                169u
#define LINUX_SYS_SETHOSTNAME           170u
#define LINUX_SYS_SETDOMAINNAME         171u
#define LINUX_SYS_IOPL                  172u
#define LINUX_SYS_IOPERM                173u
#define LINUX_SYS_CREATE_MODULE         174u
#define LINUX_SYS_INIT_MODULE           175u
#define LINUX_SYS_DELETE_MODULE         176u
#define LINUX_SYS_GET_KERNEL_SYMS       177u
#define LINUX_SYS_QUERY_MODULE          178u
#define LINUX_SYS_QUOTACTL              179u
#define LINUX_SYS_NFSSERVCTL            180u
#define LINUX_SYS_GETPMSG               181u
#define LINUX_SYS_PUTPMSG               182u
#define LINUX_SYS_AFS_SYSCALL           183u
#define LINUX_SYS_TUXCALL               184u
#define LINUX_SYS_SECURITY              185u
#define LINUX_SYS_GETTID                186u
#define LINUX_SYS_READAHEAD             187u
#define LINUX_SYS_SETXATTR              188u
#define LINUX_SYS_LSETXATTR             189u
#define LINUX_SYS_FSETXATTR             190u
#define LINUX_SYS_GETXATTR              191u
#define LINUX_SYS_LGETXATTR             192u
#define LINUX_SYS_FGETXATTR             193u
#define LINUX_SYS_LISTXATTR             194u
#define LINUX_SYS_LLISTXATTR            195u
#define LINUX_SYS_FLISTXATTR            196u
#define LINUX_SYS_REMOVEXATTR           197u
#define LINUX_SYS_LREMOVEXATTR          198u
#define LINUX_SYS_FREMOVEXATTR          199u
#define LINUX_SYS_TKILL                 200u
#define LINUX_SYS_TIME                  201u
#define LINUX_SYS_FUTEX                 202u
#define LINUX_SYS_SCHED_SETAFFINITY     203u
#define LINUX_SYS_SCHED_GETAFFINITY     204u
#define LINUX_SYS_SET_THREAD_AREA       205u
#define LINUX_SYS_IO_SETUP              206u
#define LINUX_SYS_IO_DESTROY            207u
#define LINUX_SYS_IO_GETEVENTS          208u
#define LINUX_SYS_IO_SUBMIT             209u
#define LINUX_SYS_IO_CANCEL             210u
#define LINUX_SYS_GET_THREAD_AREA       211u
#define LINUX_SYS_LOOKUP_DCOOKIE        212u
#define LINUX_SYS_EPOLL_CREATE          213u
#define LINUX_SYS_EPOLL_CTL_OLD         214u
#define LINUX_SYS_EPOLL_WAIT_OLD        215u
#define LINUX_SYS_REMAP_FILE_PAGES      216u
#define LINUX_SYS_GETDENTS64            217u
#define LINUX_SYS_SET_TID_ADDRESS       218u
#define LINUX_SYS_RESTART_SYSCALL       219u
#define LINUX_SYS_SEMTIMEDOP            220u
#define LINUX_SYS_FADVISE64             221u
#define LINUX_SYS_TIMER_CREATE          222u
#define LINUX_SYS_TIMER_SETTIME         223u
#define LINUX_SYS_TIMER_GETTIME         224u
#define LINUX_SYS_TIMER_GETOVERRUN      225u
#define LINUX_SYS_TIMER_DELETE          226u
#define LINUX_SYS_CLOCK_SETTIME         227u
#define LINUX_SYS_CLOCK_GETTIME         228u
#define LINUX_SYS_CLOCK_GETRES          229u
#define LINUX_SYS_CLOCK_NANOSLEEP       230u
#define LINUX_SYS_EXIT_GROUP            231u
#define LINUX_SYS_EPOLL_WAIT            232u
#define LINUX_SYS_EPOLL_CTL             233u
#define LINUX_SYS_TGKILL                234u
#define LINUX_SYS_UTIMES                235u
#define LINUX_SYS_VSERVER               236u
#define LINUX_SYS_MBIND                 237u
#define LINUX_SYS_SET_MEMPOLICY         238u
#define LINUX_SYS_GET_MEMPOLICY         239u
#define LINUX_SYS_MQ_OPEN               240u
#define LINUX_SYS_MQ_UNLINK             241u
#define LINUX_SYS_MQ_TIMEDSEND          242u
#define LINUX_SYS_MQ_TIMEDRECEIVE       243u
#define LINUX_SYS_MQ_NOTIFY             244u
#define LINUX_SYS_MQ_GETSETATTR         245u
#define LINUX_SYS_KEXEC_LOAD            246u
#define LINUX_SYS_WAITID                247u
#define LINUX_SYS_ADD_KEY               248u
#define LINUX_SYS_REQUEST_KEY           249u
#define LINUX_SYS_KEYCTL                250u
#define LINUX_SYS_IOPRIO_SET            251u
#define LINUX_SYS_IOPRIO_GET            252u
#define LINUX_SYS_INOTIFY_INIT          253u
#define LINUX_SYS_INOTIFY_ADD_WATCH     254u
#define LINUX_SYS_INOTIFY_RM_WATCH      255u
#define LINUX_SYS_MIGRATE_PAGES         256u
#define LINUX_SYS_OPENAT                257u
#define LINUX_SYS_MKDIRAT               258u
#define LINUX_SYS_MKNODAT               259u
#define LINUX_SYS_FCHOWNAT              260u
#define LINUX_SYS_FUTIMESAT             261u
#define LINUX_SYS_NEWFSTATAT            262u
#define LINUX_SYS_UNLINKAT              263u
#define LINUX_SYS_RENAMEAT              264u
#define LINUX_SYS_LINKAT                265u
#define LINUX_SYS_SYMLINKAT             266u
#define LINUX_SYS_READLINKAT            267u
#define LINUX_SYS_FCHMODAT              268u
#define LINUX_SYS_FACCESSAT             269u
#define LINUX_SYS_PSELECT6              270u
#define LINUX_SYS_PPOLL                 271u
#define LINUX_SYS_UNSHARE               272u
#define LINUX_SYS_SET_ROBUST_LIST       273u
#define LINUX_SYS_GET_ROBUST_LIST       274u
#define LINUX_SYS_SPLICE                275u
#define LINUX_SYS_TEE                   276u
#define LINUX_SYS_SYNC_FILE_RANGE       277u
#define LINUX_SYS_VMSPLICE              278u
#define LINUX_SYS_MOVE_PAGES            279u
#define LINUX_SYS_UTIMENSAT             280u
#define LINUX_SYS_EPOLL_PWAIT           281u
#define LINUX_SYS_SIGNALFD              282u
#define LINUX_SYS_TIMERFD_CREATE        283u
#define LINUX_SYS_EVENTFD               284u
#define LINUX_SYS_FALLOCATE             285u
#define LINUX_SYS_TIMERFD_SETTIME       286u
#define LINUX_SYS_TIMERFD_GETTIME       287u
#define LINUX_SYS_ACCEPT4               288u
#define LINUX_SYS_SIGNALFD4             289u
#define LINUX_SYS_EVENTFD2              290u
#define LINUX_SYS_EPOLL_CREATE1         291u
#define LINUX_SYS_DUP3                  292u
#define LINUX_SYS_PIPE2                 293u
#define LINUX_SYS_INOTIFY_INIT1         294u
#define LINUX_SYS_PREADV                295u
#define LINUX_SYS_PWRITEV               296u
#define LINUX_SYS_RT_TGSIGQUEUEINFO     297u
#define LINUX_SYS_PERF_EVENT_OPEN       298u
#define LINUX_SYS_RECVMMSG              299u
#define LINUX_SYS_FANOTIFY_INIT         300u
#define LINUX_SYS_FANOTIFY_MARK         301u
#define LINUX_SYS_PRLIMIT64             302u
#define LINUX_SYS_NAME_TO_HANDLE_AT     303u
#define LINUX_SYS_OPEN_BY_HANDLE_AT     304u
#define LINUX_SYS_CLOCK_ADJTIME         305u
#define LINUX_SYS_SYNCFS                306u
#define LINUX_SYS_SENDMMSG              307u
#define LINUX_SYS_SETNS                 308u
#define LINUX_SYS_GETCPU                309u
#define LINUX_SYS_PROCESS_VM_READV      310u
#define LINUX_SYS_PROCESS_VM_WRITEV     311u
#define LINUX_SYS_KCMP                  312u
#define LINUX_SYS_FINIT_MODULE          313u
#define LINUX_SYS_SCHED_SETATTR         314u
#define LINUX_SYS_SCHED_GETATTR         315u
#define LINUX_SYS_RENAMEAT2             316u
#define LINUX_SYS_SECCOMP               317u
#define LINUX_SYS_GETRANDOM             318u
#define LINUX_SYS_MEMFD_CREATE          319u
#define LINUX_SYS_KEXEC_FILE_LOAD       320u
#define LINUX_SYS_BPF                   321u
#define LINUX_SYS_EXECVEAT              322u
#define LINUX_SYS_USERFAULTFD           323u
#define LINUX_SYS_MEMBARRIER            324u
#define LINUX_SYS_MLOCK2                325u
#define LINUX_SYS_COPY_FILE_RANGE       326u
#define LINUX_SYS_PREADV2               327u
#define LINUX_SYS_PWRITEV2              328u
#define LINUX_SYS_PKEY_MPROTECT         329u
#define LINUX_SYS_PKEY_ALLOC            330u
#define LINUX_SYS_PKEY_FREE             331u
#define LINUX_SYS_STATX                 332u
#define LINUX_SYS_IO_PGETEVENTS         333u
#define LINUX_SYS_RSEQ                  334u
#define LINUX_SYS_PIDFD_SEND_SIGNAL     424u
#define LINUX_SYS_IO_URING_SETUP        425u
#define LINUX_SYS_IO_URING_ENTER        426u
#define LINUX_SYS_IO_URING_REGISTER     427u
#define LINUX_SYS_OPEN_TREE             428u
#define LINUX_SYS_MOVE_MOUNT            429u
#define LINUX_SYS_FSOPEN                430u
#define LINUX_SYS_FSCONFIG              431u
#define LINUX_SYS_FSMOUNT               432u
#define LINUX_SYS_FSPICK                433u
#define LINUX_SYS_PIDFD_OPEN            434u
#define LINUX_SYS_CLONE3                435u
#define LINUX_SYS_CLOSE_RANGE           436u
#define LINUX_SYS_OPENAT2               437u
#define LINUX_SYS_PIDFD_GETFD           438u
#define LINUX_SYS_FACCESSAT2            439u
#define LINUX_SYS_PROCESS_MADVISE       440u
#define LINUX_SYS_EPOLL_PWAIT2          441u
#define LINUX_SYS_MOUNT_SETATTR         442u
#define LINUX_SYS_QUOTACTL_FD           443u
#define LINUX_SYS_LANDLOCK_CREATE_RULESET 444u
#define LINUX_SYS_LANDLOCK_ADD_RULE     445u
#define LINUX_SYS_LANDLOCK_RESTRICT_SELF 446u
#define LINUX_SYS_MEMFD_SECRET          447u
#define LINUX_SYS_PROCESS_MRELEASE      448u
#define LINUX_SYS_FUTEX_WAITV           449u
#define LINUX_SYS_SET_MEMPOLICY_HOME_NODE 450u
#define LINUX_SYS_CACHESTAT             451u
#define LINUX_SYS_FCHMODAT2             452u
#define LINUX_SYS_MAP_SHADOW_STACK      453u
#define LINUX_SYS_FUTEX_WAKE            454u
#define LINUX_SYS_FUTEX_WAIT            455u

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

/* ── Linux arch_prctl operations ── */
#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

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

/* ── Linux Futex Operations ── */
#define LINUX_FUTEX_WAIT            0
#define LINUX_FUTEX_WAKE            1
#define LINUX_FUTEX_FD              2
#define LINUX_FUTEX_REQUEUE         3
#define LINUX_FUTEX_CMP_REQUEUE     4
#define LINUX_FUTEX_WAKE_OP         5
#define LINUX_FUTEX_LOCK_PI         6
#define LINUX_FUTEX_UNLOCK_PI       7
#define LINUX_FUTEX_TRYLOCK_PI      8
#define LINUX_FUTEX_WAIT_BITSET     9
#define LINUX_FUTEX_WAKE_BITSET    10
#define LINUX_FUTEX_WAIT_REQUEUE_PI 11
#define LINUX_FUTEX_CMP_REQUEUE_PI 12
#define LINUX_FUTEX_PRIVATE_FLAG  128
#define LINUX_FUTEX_CLOCK_REALTIME 256

/* ── Linux Compatibility Layer APIs ── */
int linux_compat_init(void);
int linux_sandbox_create(u32 max_mem_mb, u32 max_fds, u8 seccomp_strict, const char* root_path);
void linux_sandbox_destroy(int sandbox_id);
linux_sandbox_t* linux_sandbox_get(int sandbox_id);

int linux_resolve_path(const char* in_path, char* out_path, u32 out_len);
int linux_futex_op(u64 uaddr, int futex_op, u32 val, u64 timeout_ptr, u64 uaddr2, u32 val3);

/* ── Linux Graphical Display Server Bridge (X11 / Wayland Socket) ── */
typedef struct {
    u32 window_id;
    u32 width;
    u32 height;
    u32 pos_x;
    u32 pos_y;
    u8  is_mapped;
    u32 shm_id;
    void* framebuffer;
    char title[64];
} linux_gui_window_t;

int  linux_display_server_init(void);
int  linux_display_connect_client(int client_fd);
void linux_display_process_packet(int client_fd, const u8* data, u32 len);
int  linux_display_window_count(void);

i64 linux_syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, int sandbox_id);
i64 x86_64_syscall_handler(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6);
u64 linux_setup_user_stack(u8* stack_buf, u64 stack_top_vaddr,
                           int argc, const char** argv, const char** envp,
                           u64 entry_point, u64 phdr_addr, u16 phnum, u16 phent);
int linux_run_binary(const u8* elf_bytes, u32 elf_size, const char* name, int sandbox_id);

#ifdef __cplusplus
}
#endif

#endif /* LINUX_COMPAT_H */
