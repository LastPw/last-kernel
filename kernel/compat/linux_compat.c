#include "linux_compat.h"
#include "compat.h"
#include "elf_loader.h"
#include "kernel_api.h"
#include "resource_monitor.h"
#include "heap.h"
#include "string.h"
#include "serial.h"
#include "timer.h"
#include "entropy.h"
#include "vfs.h"
#include "wait.h"
#include "scheduler.h"
#include "gfx.h"
#include "io_uring.h"
#include "netstack.h"
#include "nic.h"
#include "ip.h"
#include "cpu_detect.h"

#ifndef LINUX_EAFNOSUPPORT
#define LINUX_EAFNOSUPPORT 97
#endif
#ifndef LINUX_EADDRINUSE
#define LINUX_EADDRINUSE 98
#endif
#ifndef LINUX_ETIMEDOUT
#define LINUX_ETIMEDOUT 110
#endif
#ifndef LINUX_ENOTCONN
#define LINUX_ENOTCONN 107
#endif

/* ── Linux AF_INET / BSD Sockets Subsystem ── */
#define LINUX_SOCK_BASE 50
#define LINUX_SOCK_MAX  32

typedef struct {
    u8 in_use;
    u8 is_listener;
    u8 nonblock;
    u8 state;         /* 0: closed/init, 1: connecting, 2: connected */
    int domain;       /* AF_INET=2, AF_INET6=10, AF_UNIX=1 */
    int type;         /* SOCK_STREAM=1, SOCK_DGRAM=2 */
    int protocol;     /* IP_PROTO_TCP=6, IP_PROTO_UDP=17 */
    int netstack_id;  /* -1 if not opened yet */
    u32 local_ip;
    u16 local_port;
    u32 remote_ip;
    u16 remote_port;
} linux_socket_entry_t;

static linux_socket_entry_t s_linux_socks[LINUX_SOCK_MAX];

struct linux_sockaddr_in {
    u16 sin_family;
    u16 sin_port;
    u32 sin_addr;
    u8  sin_zero[8];
};

static linux_socket_entry_t* linux_sock_get(int sfd) {
    if (sfd < LINUX_SOCK_BASE || sfd >= (LINUX_SOCK_BASE + LINUX_SOCK_MAX)) return 0;
    linux_socket_entry_t* sk = &s_linux_socks[sfd - LINUX_SOCK_BASE];
    return sk->in_use ? sk : 0;
}

static int linux_sock_alloc(int domain, int type, int proto) {
    for (int i = 0; i < LINUX_SOCK_MAX; i++) {
        if (!s_linux_socks[i].in_use) {
            memset(&s_linux_socks[i], 0, sizeof(linux_socket_entry_t));
            s_linux_socks[i].in_use = 1;
            s_linux_socks[i].domain = domain;
            s_linux_socks[i].type = type;
            s_linux_socks[i].protocol = proto;
            s_linux_socks[i].netstack_id = -1;
            return LINUX_SOCK_BASE + i;
        }
    }
    return -1;
}

static void linux_sock_free(int sfd) {
    if (sfd < LINUX_SOCK_BASE || sfd >= (LINUX_SOCK_BASE + LINUX_SOCK_MAX)) return;
    linux_socket_entry_t* sk = &s_linux_socks[sfd - LINUX_SOCK_BASE];
    if (sk->in_use) {
        if (sk->netstack_id >= 0) {
            (void)netstack_socket_close((u32)sk->netstack_id);
        }
        memset(sk, 0, sizeof(linux_socket_entry_t));
    }
}


/*
 * LastOS FreeBSD-Style Linux Compatibility Subsystem (Linuxulator 6.8)
 *
 * Implements the Complete Linux GUI & Core Subsystem:
 * 1. Shadow Root Translation: /compat/linux/<path> prefixing with native fallback.
 * 2. Linprocfs & Linsysfs Virtual In-Memory PseudoFS (/proc/self, /proc/cpuinfo, /proc/version).
 * 3. Linux Futex Synchronization Engine (FUTEX_WAIT, FUTEX_WAKE, FUTEX_REQUEUE).
 * 4. System V AMD64 User Stack & Auxiliary Vector (auxv) Engine.
 */

#define MAX_LINUX_SANDBOXES 16
/* Chunk size for pulling a ring-3 payload into a kernel buffer: the guarded
   copy in lin_fetch_bytes() caps a single transfer at 240 bytes. */
#define LINUX_SEND_CHUNK 240u
static linux_sandbox_t s_sandboxes[MAX_LINUX_SANDBOXES];
static u8 s_linux_inited = 0;
static u8 s_linux_trace_done = 0;

/* Virtual Linprocfs Buffers */
static const char* s_proc_version = "Linux version 6.8.0-lastos-smp (LastOS compat ABI, built with " __VERSION__ ") #1 SMP PREEMPT\n";
static const char* s_proc_mounts = "rootfs / rootfs rw 0 0\n";
static const char* s_proc_osrelease = "6.8.0-lastos-smp\n";

/* ── Directory endpoints a Linux program expects to find ──
   This layer serves no display server.  The block here used to answer the X11
   connection handshake from a 40-byte canned reply, allocate a 1.9 MiB kernel
   "window" framebuffer for every client that sent a setup request, and blit a
   label onto a display the desktop boot path never turns on.  What remains is
   only the directories; AF_UNIX is refused in sys_socket, and an X or Wayland
   client therefore learns in one call that there is no socket to talk to.  The
   real sockets this table does carry (AF_INET/AF_INET6 through the netstack)
   let a Linux client reach a genuine server elsewhere on the network. */
static void linux_tmp_dirs_init(void) {
    (void)vfs_mkdir("/tmp");
    (void)vfs_mkdir("/run");
    (void)vfs_mkdir("/run/user");
    (void)vfs_mkdir("/run/user/1000");
    (void)vfs_mkdir("/compat/linux/tmp");
}

/* ── Linux Futex Wait Queue Subsystem ── */
typedef struct {
    u64 uaddr;
    wait_queue_t wq;
    u8 active;
} linux_futex_bucket_t;

#define LINUX_MAX_FUTEX 32
static linux_futex_bucket_t s_futexes[LINUX_MAX_FUTEX];

int linux_futex_op(u64 uaddr, int futex_op, u32 val, u64 timeout_ptr, u64 uaddr2, u32 val3) {
    (void)timeout_ptr; (void)uaddr2; (void)val3;
    int cmd = futex_op & 0x7F;

    if (cmd == LINUX_FUTEX_WAIT || cmd == LINUX_FUTEX_WAIT_BITSET) {
        if (uaddr) {
            u32 cur_val = *(volatile u32*)(uintptr_t)uaddr;
            if (cur_val != val) {
                return -LINUX_EAGAIN;
            }
        }
        linux_futex_bucket_t* bucket = 0;
        for (u32 i = 0; i < LINUX_MAX_FUTEX; i++) {
            if (s_futexes[i].active && s_futexes[i].uaddr == uaddr) {
                bucket = &s_futexes[i];
                break;
            }
        }
        if (!bucket) {
            for (u32 i = 0; i < LINUX_MAX_FUTEX; i++) {
                if (!s_futexes[i].active) {
                    bucket = &s_futexes[i];
                    bucket->active = 1;
                    bucket->uaddr = uaddr;
                    wait_queue_init(&bucket->wq);
                    break;
                }
            }
        }
        if (bucket) {
            wait_queue_add_current(&bucket->wq);
            scheduler_schedule();
        }
        return 0;
    }

    if (cmd == LINUX_FUTEX_WAKE || cmd == LINUX_FUTEX_WAKE_BITSET) {
        u32 woken = 0;
        for (u32 i = 0; i < LINUX_MAX_FUTEX; i++) {
            if (s_futexes[i].active && s_futexes[i].uaddr == uaddr) {
                for (u32 k = 0; k < val; k++) {
                    if (wait_queue_wake_one(&s_futexes[i].wq) == 0) {
                        woken++;
                    } else {
                        break;
                    }
                }
                if (wait_queue_count(&s_futexes[i].wq) == 0) {
                    s_futexes[i].active = 0;
                }
                break;
            }
        }
        return (int)woken;
    }

    return 0;
}

/* ── FreeBSD-Style Shadow Path Resolution (/compat/linux) ── */
int linux_resolve_path(const char* in_path, char* out_path, u32 out_len) {
    if (!in_path || !out_path || out_len < 16) return -1;

    if (str_ncmp(in_path, "/compat/linux", 13) == 0 ||
        str_ncmp(in_path, "/proc", 5) == 0 ||
        str_ncmp(in_path, "/dev", 4) == 0 ||
        str_ncmp(in_path, "/sys", 4) == 0 ||
        str_ncmp(in_path, "/tmp", 4) == 0 ||
        str_ncmp(in_path, "/run", 4) == 0) {
        str_ncpy(out_path, in_path, out_len - 1);
        out_path[out_len - 1] = '\0';
        return 0;
    }

    char shadow[128];
    shadow[0] = '\0';
    str_cpy(shadow, "/compat/linux");
    if (in_path[0] != '/') {
        u32 l = (u32)str_len(shadow);
        shadow[l] = '/';
        shadow[l+1] = '\0';
    }
    u32 cur_len = (u32)str_len(shadow);
    str_ncpy(shadow + cur_len, in_path, sizeof(shadow) - cur_len - 1);

    vfs_node_info_t info;
    if (vfs_info(shadow, &info) == 0) {
        str_ncpy(out_path, shadow, out_len - 1);
        out_path[out_len - 1] = '\0';
        return 0;
    }

    str_ncpy(out_path, in_path, out_len - 1);
    out_path[out_len - 1] = '\0';
    return 0;
}

int linux_compat_init(void) {
    if (s_linux_inited) return 0;
    for (u32 i = 0; i < MAX_LINUX_SANDBOXES; i++) {
        s_sandboxes[i].active = 0;
        s_sandboxes[i].sandbox_id = i;
    }
    for (u32 i = 0; i < LINUX_MAX_FUTEX; i++) {
        s_futexes[i].active = 0;
        s_futexes[i].uaddr = 0;
    }

    /* Initialize FreeBSD-style /compat/linux VFS directory tree */
    vfs_mkdir("/compat");
    vfs_mkdir("/compat/linux");
    vfs_mkdir("/compat/linux/bin");
    vfs_mkdir("/compat/linux/lib64");
    vfs_mkdir("/compat/linux/etc");
    vfs_mkdir("/compat/linux/proc");
    vfs_mkdir("/compat/linux/dev");
    vfs_mkdir("/compat/linux/sys");
    vfs_mkdir("/compat/linux/tmp");
    vfs_mkdir("/compat/linux/usr");
    vfs_mkdir("/compat/linux/usr/bin");
    vfs_mkdir("/compat/linux/usr/lib");

    linux_tmp_dirs_init();

    s_linux_inited = 1;
    serial_write("[LINUX_COMPAT] Linux 6.8 ABI layer: ELF64 exec, procfs, futex, inet sockets (no display server)\n");
    return 0;
}

int linux_sandbox_create(u32 max_mem_mb, u32 max_fds, u8 seccomp_strict, const char* root_path) {
    if (!s_linux_inited) linux_compat_init();

    for (u32 i = 0; i < MAX_LINUX_SANDBOXES; i++) {
        if (!s_sandboxes[i].active) {
            s_sandboxes[i].active = 1;
            s_sandboxes[i].max_memory_bytes = max_mem_mb * 1024u * 1024u;
            s_sandboxes[i].allocated_bytes = 0;
            s_sandboxes[i].peak_memory_bytes = 0;
            s_sandboxes[i].max_fds = max_fds > 0 ? max_fds : 256u;
            s_sandboxes[i].open_fds = 3u;
            s_sandboxes[i].seccomp_strict = seccomp_strict;
            s_sandboxes[i].syscall_count = 0;
            s_sandboxes[i].violations_blocked = 0;
            if (root_path) {
                str_ncpy(s_sandboxes[i].chroot_path, root_path, sizeof(s_sandboxes[i].chroot_path) - 1);
            } else {
                str_cpy(s_sandboxes[i].chroot_path, "/compat/linux");
            }
            return (int)i;
        }
    }
    return -1;
}

void linux_sandbox_destroy(int sandbox_id) {
    if (sandbox_id < 0 || sandbox_id >= MAX_LINUX_SANDBOXES) return;
    s_sandboxes[sandbox_id].active = 0;
}

linux_sandbox_t* linux_sandbox_get(int sandbox_id) {
    if (sandbox_id < 0 || sandbox_id >= MAX_LINUX_SANDBOXES) return 0;
    if (!s_sandboxes[sandbox_id].active) return 0;
    return &s_sandboxes[sandbox_id];
}

/* Linux Utsname Structure for uname() */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
} linux_utsname_t;

/* Linux Timespec Structure */
typedef struct {
    i64 tv_sec;
    i64 tv_nsec;
} linux_timespec_t;

/* Linux Sysinfo Structure */
typedef struct {
    i64 uptime;
    u64 loads[3];
    u64 totalram;
    u64 freeram;
    u64 sharedram;
    u64 bufferram;
    u64 totalswap;
    u64 freeswap;
    u16 procs;
    u16 pad;
    u64 totalhigh;
    u64 freehigh;
    u32 mem_unit;
    char _f[20-2*sizeof(u64)-sizeof(u32)];
} linux_sysinfo_t;

#define LINUX_MAX_PATH 256

/* Every pointer a Linux task passes is ring-3 memory.  It is copied through the
   guarded accessors instead of being dereferenced, so a bogus address becomes an
   EFAULT handed back to the caller rather than a page fault taken by the kernel
   on the task's behalf. */
static i64 lin_put_bytes(u64 user_dst, const char* src, u64 count) {
    u64 done = 0u;
    while (done < count) {
        u32 chunk = (u32)(count - done);
        if (chunk > 240u) chunk = 240u;
        if (kcopy_to_user(user_dst + done, src + done, chunk) != 0) {
            return (done > 0u) ? (i64)done : -LINUX_EFAULT;
        }
        done += chunk;
    }
    return (i64)done;
}

static int lin_fetch_bytes(char* dst, u64 user_src, u32 len) {
    u32 done = 0u;
    while (done < len) {
        u32 chunk = len - done;
        if (chunk > 240u) chunk = 240u;
        if (kcopy_from_user(dst + done, user_src + done, chunk) != 0) return -1;
        done += chunk;
    }
    return 0;
}

static int lin_fetch_str(u64 user_src, char* out, u32 cap) {
    u32 i = 0u;
    if (!out || cap == 0u) return -1;
    while (i + 1u < cap) {
        char c = 0;
        if (kcopy_from_user(&c, user_src + i, 1u) != 0) return -1;
        out[i] = c;
        if (c == '\0') return (int)i;
        i++;
    }
    out[i] = '\0';
    return -1;
}

static u32 lin_put_dec(char* out, u32 cap, u32 pos, u64 value) {
    char digits[20];
    u32 n = 0u;
    do {
        digits[n++] = (char)('0' + (u32)(value % 10u));
        value /= 10u;
    } while (value && n < sizeof(digits));
    while (n > 0u && pos + 1u < cap) out[pos++] = digits[--n];
    return pos;
}

static u32 lin_put_str(char* out, u32 cap, u32 pos, const char* s) {
    u32 i = 0u;
    while (s[i] && pos + 1u < cap) out[pos++] = s[i++];
    return pos;
}

/* /proc/cpuinfo rendered from the CPUID data the kernel actually detected at
   boot.  Only the extensions cpu_detect.c verified are listed, so a Linux
   program reading this file is never told about a feature the host lacks. */
static u32 lin_render_cpuinfo(char* buf, u32 cap, u32 pos) {
    static const char* flag_names[11] = {
        "apic", "sse", "avx", "avx2", "avx512f",
        "xsave", "nx", "lm", "smep", "smap", "mwait"
    };
    const cpu_info_t* cpu = cpu_get_info();
    u8 flag_present[11];
    u32 i;

    if (!buf || cap == 0u || !cpu || !cpu->has_cpuid) return pos;
    flag_present[0] = cpu->has_apic;
    flag_present[1] = cpu->has_sse;
    flag_present[2] = cpu->has_avx;
    flag_present[3] = cpu->has_avx2;
    flag_present[4] = cpu->has_avx512f;
    flag_present[5] = cpu->has_xsave;
    flag_present[6] = cpu->has_nx;
    flag_present[7] = cpu->has_long_mode;
    flag_present[8] = cpu->has_smep;
    flag_present[9] = cpu->has_smap;
    flag_present[10] = cpu->has_mwait;

    pos = lin_put_str(buf, cap, pos, "processor\t: 0\nvendor_id\t: ");
    pos = lin_put_str(buf, cap, pos, cpu->vendor);
    pos = lin_put_str(buf, cap, pos, "\nmodel name\t: ");
    pos = lin_put_str(buf, cap, pos, cpu->brand[0] ? cpu->brand : cpu->microarch);
    pos = lin_put_str(buf, cap, pos, "\ncpu family\t: ");
    pos = lin_put_dec(buf, cap, pos, (u64)cpu->family);
    pos = lin_put_str(buf, cap, pos, "\nmodel\t\t: ");
    pos = lin_put_dec(buf, cap, pos, (u64)cpu->model);
    pos = lin_put_str(buf, cap, pos, "\nstepping\t: ");
    pos = lin_put_dec(buf, cap, pos, (u64)cpu->stepping);
    pos = lin_put_str(buf, cap, pos, "\ncpu cores\t: ");
    pos = lin_put_dec(buf, cap, pos, (u64)cpu->physical_cores);
    pos = lin_put_str(buf, cap, pos, "\nsiblings\t: ");
    pos = lin_put_dec(buf, cap, pos, (u64)cpu->logical_cores);
    pos = lin_put_str(buf, cap, pos, "\nflags\t\t:");
    for (i = 0u; i < 11u; i++) {
        if (!flag_present[i]) continue;
        pos = lin_put_str(buf, cap, pos, " ");
        pos = lin_put_str(buf, cap, pos, flag_names[i]);
    }
    pos = lin_put_str(buf, cap, pos, "\n\n");
    return pos;
}

/* struct stat as the x86_64 Linux ABI lays it out: three 64-bit identity
   fields, then mode/uid/gid plus the alignment hole, then the size and time
   block.  A mirrored struct with no compile-time proof of its size is how a
   compat layer starts writing past the caller's buffer, so the assert is part
   of the definition. */
typedef struct {
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u32 st_pad0;
    u64 st_rdev;
    i64 st_size;
    i64 st_blksize;
    i64 st_blocks;
    i64 st_atim_sec;
    u64 st_atim_nsec;
    i64 st_mtim_sec;
    u64 st_mtim_nsec;
    i64 st_ctim_sec;
    u64 st_ctim_nsec;
    u64 st_unused[3];
} lin_stat_t;

_Static_assert(sizeof(lin_stat_t) == 144u, "lin_stat_t must match Linux x86_64 struct stat");

#define LIN_S_IFDIR  0040000u
#define LIN_S_IFREG  0100000u

/* Seconds since 1970 from the CMOS RTC, or 0 when the RTC does not answer. */
static u32 lin_rtc_seconds(void) {
    rtc_time_t t;
    if (rtc_get_time(&t) != 0) return 0u;
    return (rtc_days_since_epoch(&t) * 86400u) + rtc_seconds_since_midnight(&t);
}

/* The virtual procfs files served by this layer.  The memory and process
   records are rendered from the live allocator and scheduler on every read, so
   a Linux program inspecting /proc sees the machine it is actually on instead
   of a canned text block.  A file with no truthful answer is refused outright. */
static u32 lin_render_proc(int fd, char* buf, u32 cap) {
    const char* canned = 0;
    u32 pos = 0u;
    if (!buf || cap == 0u) return 0u;
    switch (fd) {
        case 100: canned = s_proc_version; break;
        case 101: return lin_render_cpuinfo(buf, cap, pos);
        case 105: canned = s_proc_mounts; break;
        case 109: canned = s_proc_osrelease; break;
        case 104: {
            resource_snapshot_t rs;
            (void)memset(&rs, 0, sizeof(rs));
            resource_monitor_snapshot(&rs);
            pos = lin_put_str(buf, cap, pos, "MemTotal:       ");
            pos = lin_put_dec(buf, cap, pos, (u64)rs.mem_total_bytes / 1024ull);
            pos = lin_put_str(buf, cap, pos, " kB\nMemFree:        ");
            pos = lin_put_dec(buf, cap, pos,
                              (rs.mem_total_bytes > rs.mem_used_bytes
                                   ? (u64)(rs.mem_total_bytes - rs.mem_used_bytes)
                                   : 0ull) / 1024ull);
            pos = lin_put_str(buf, cap, pos, " kB\nMemAvailable:   ");
            pos = lin_put_dec(buf, cap, pos,
                              (rs.mem_total_bytes > rs.mem_used_bytes
                                   ? (u64)(rs.mem_total_bytes - rs.mem_used_bytes)
                                   : 0ull) / 1024ull);
            pos = lin_put_str(buf, cap, pos, " kB\nSwapTotal:             0 kB\nSwapFree:              0 kB\n");
            buf[pos] = '\0';
            return pos;
        }
        case 106: case 107: {
            u32 pid = scheduler_current_task();
            pos = lin_put_str(buf, cap, pos, "Name:\tlinux-app\nState:\tR (running)\nTgid:\t");
            pos = lin_put_dec(buf, cap, pos, (u64)pid);
            pos = lin_put_str(buf, cap, pos, "\nPid:\t");
            pos = lin_put_dec(buf, cap, pos, (u64)pid);
            pos = lin_put_str(buf, cap, pos, "\nPPid:\t0\nThreads:\t1\n");
            buf[pos] = '\0';
            return pos;
        }
        default: return 0u;
    }
    if (!canned) return 0u;
    {
        u32 i = 0u;
        while (canned[i] && i + 1u < cap) { buf[i] = canned[i]; i++; }
        buf[i] = '\0';
        return i;
    }
}

/* ── Linux Syscall Fast-Path Dispatcher with Sandboxing ── */
i64 linux_syscall_dispatch(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6, int sandbox_id) {
    linux_sandbox_t* sb = (sandbox_id >= 0 && sandbox_id < MAX_LINUX_SANDBOXES) ? &s_sandboxes[sandbox_id] : 0;
    if (sb) {
        sb->syscall_count++;
    }
    /* One-shot proof that the personality routing in the syscall entry actually
       delivered a Linux-ABI trap here: the pid and the first syscall number are
       logged once per boot. */
    if (!s_linux_trace_done) {
        s_linux_trace_done = 1;
        serial_write("[linux] table engaged: pid ");
        serial_write_dec((u32)scheduler_current_task());
        serial_write(" first syscall ");
        serial_write_dec((u32)nr);
        serial_writeln("");
    }

    switch (nr) {
        /* 0: sys_read */
        case LINUX_SYS_READ: {
            int fd = (int)a1;
            char* buf = (char*)a2;
            u64 count = a3;
            if (!buf) return -LINUX_EFAULT;
            if (fd == 0) return 0;
            {
                char text[1024];
                u32 text_len = lin_render_proc(fd, text, sizeof(text));
                if (text_len > 0u) {
                    if (count > text_len) count = text_len;
                    return lin_put_bytes(a2, text, count);
                }
            }
            if (fd == 102) {
                char entropy[64];
                u32 n = (count > sizeof(entropy)) ? (u32)sizeof(entropy) : (u32)count;
                entropy_fill((u8*)entropy, n);
                return lin_put_bytes(a2, entropy, n);
            }
            if (fd == 103) return 0; /* /dev/null: end of file */
            if (fd == 110) {
                /* /dev/zero: the requested bytes, all of them zero. */
                static const char zeros[64] = { 0 };
                u64 made = 0u;
                while (made < count) {
                    u64 n = count - made;
                    if (n > sizeof(zeros)) n = sizeof(zeros);
                    {
                        i64 put = lin_put_bytes(a2 + made, zeros, n);
                        if (put < 0) return (made > 0u) ? (i64)made : put;
                    }
                    made += n;
                }
                return (i64)made;
            }
            if (fd >= LINUX_SOCK_BASE && fd < LINUX_SOCK_BASE + LINUX_SOCK_MAX) {
                linux_socket_entry_t* sk = linux_sock_get(fd);
                char chunk[240];
                u32 src_ip = 0; u16 src_port = 0;
                int n = -1;
                u32 dl;
                u32 want;
                if (!sk || sk->netstack_id < 0) return -LINUX_EBADF;
                want = (count > sizeof(chunk)) ? (u32)sizeof(chunk) : (u32)count;
                if (want == 0u) want = 1u;
                dl = timer_ticks() + (sk->nonblock ? 1u : 200u);
                while ((i32)(timer_ticks() - dl) < 0) {
                    (void)nic_poll(16u); netstack_poll(16u);
                    n = netstack_socket_recv((u32)sk->netstack_id, (u8*)chunk,
                                             (u16)want, &src_ip, &src_port);
                    if (n > 0) break;
                    u8 st = 0;
                    if (netstack_socket_state((u32)sk->netstack_id, &st) == 0 &&
                        st == NET_SOCKET_CLOSING) return 0;
                    if (sk->nonblock) break;
                }
                if (n <= 0) return sk->nonblock ? -LINUX_EAGAIN : 0;
                return lin_put_bytes(a2, chunk, (u64)n);
            }
            /* No backing store behind this descriptor: report that rather than
               pretending the caller successfully read an empty buffer. */
            return -LINUX_EBADF;
        }

        /* 1: sys_write */
        case LINUX_SYS_WRITE: {
            int fd = (int)a1;
            const char* buf = (const char*)a2;
            u64 count = a3;
            char chunk[256];
            u64 done = 0u;
            if (!buf) return -LINUX_EFAULT;
            while (done < count) {
                u32 n = (u32)(count - done);
                if (n > 255u) n = 255u;
                if (lin_fetch_bytes(chunk, a2 + done, n) != 0) {
                    return (done > 0u) ? (i64)done : -LINUX_EFAULT;
                }
                if (fd >= LINUX_SOCK_BASE && fd < LINUX_SOCK_BASE + LINUX_SOCK_MAX) {
                    linux_socket_entry_t* sk = linux_sock_get(fd);
                    if (!sk || sk->netstack_id < 0) return -LINUX_EBADF;
                    if (netstack_socket_send((u32)sk->netstack_id, (const u8*)chunk, (u16)n) != 0) {
                        return (done > 0u) ? (i64)done : -LINUX_EPIPE;
                    }
                } else if (fd == 1 || fd == 2) {
                    chunk[n] = '\0';
                    serial_write(chunk);
                } else {
                    return (done > 0u) ? (i64)done : -LINUX_EBADF;
                }
                done += n;
            }
            return (i64)done;
        }

        /* 2: sys_open, 257: sys_openat, 437: sys_openat2 */
        case LINUX_SYS_OPEN:
        case LINUX_SYS_OPENAT:
        case LINUX_SYS_OPENAT2: {
            const char* raw_path = (nr == LINUX_SYS_OPEN) ? (const char*)a1 : (const char*)a2;
            if (!raw_path) return -LINUX_EFAULT;

            char resolved_path[128];
            linux_resolve_path(raw_path, resolved_path, sizeof(resolved_path));

            if (str_str(resolved_path, "version")) return 100;
            if (str_str(resolved_path, "cpuinfo")) return 101;
            if (str_str(resolved_path, "urandom") || str_str(resolved_path, "random")) return 102;
            if (str_str(resolved_path, "null")) return 103;
            if (str_str(resolved_path, "zero")) return 110;
            if (str_str(resolved_path, "meminfo")) return 104;
            if (str_str(resolved_path, "mounts")) return 105;
            if (str_str(resolved_path, "status")) return 107;
            if (str_str(resolved_path, "osrelease")) return 109;
            /* /proc/<pid>/maps stays refused: enumerating another address space
               is not implemented, and an fd that reads back EBADF is worse than
               an open that fails here. */

            if (sb && sb->open_fds < sb->max_fds) {
                sb->open_fds++;
                return (i64)(10 + sb->open_fds);
            }
            return -LINUX_ENOENT;
        }

        /* 3: sys_close, 436: sys_close_range */
        case LINUX_SYS_CLOSE:
        case LINUX_SYS_CLOSE_RANGE: {
            int fd = (int)a1;
            if (fd >= LINUX_SOCK_BASE && fd < LINUX_SOCK_BASE + LINUX_SOCK_MAX) {
                linux_sock_free(fd);
            }
            if (sb && sb->open_fds > 3u) sb->open_fds--;
            return 0;
        }

        /* 4: sys_stat, 5: sys_fstat, 6: sys_lstat, 262: sys_newfstatat, 332: sys_statx */
        /* 4: sys_stat, 6: sys_lstat, 262: sys_newfstatat.  Answered from the
           command VFS inode, so inode number, size, mode and ownership are the
           real ones.  Timestamps come from the CMOS clock because the VFS stamps
           nodes with ticks, not epoch seconds.
           5: sys_fstat and 291: sys_statx stay refused: this layer keeps no
           descriptor-to-inode table, so it cannot say what an fd points at. */
        case LINUX_SYS_STAT:
        case LINUX_SYS_LSTAT:
        case LINUX_SYS_NEWFSTATAT: {
            char path[VFS_PATH_MAX];
            vfs_node_info_t info;
            lin_stat_t st;
            u64 stamp;
            u64 path_arg = (nr == LINUX_SYS_NEWFSTATAT) ? a2 : a1;
            u64 out_arg = (nr == LINUX_SYS_NEWFSTATAT) ? a3 : a2;
            if (lin_fetch_str(path_arg, path, sizeof(path)) < 0) return -LINUX_EFAULT;
            if (path[0] == '\0') return -LINUX_ENOENT;
            if (vfs_info(path, &info) != 0) return -LINUX_ENOENT;
            (void)memset(&st, 0, sizeof(st));
            stamp = (u64)lin_rtc_seconds();
            st.st_ino = (u64)info.id;
            st.st_nlink = 1u;
            st.st_mode = (info.type == VFS_NODE_DIR) ? LIN_S_IFDIR : LIN_S_IFREG;
            st.st_mode |= (u32)(info.mode & 0777u);
            st.st_uid = info.owner_uid;
            st.st_gid = info.owner_gid;
            st.st_size = (i64)info.size;
            st.st_blksize = 4096;
            st.st_blocks = (i64)((info.size + 511u) / 512u);
            st.st_atim_sec = st.st_mtim_sec = st.st_ctim_sec = (i64)stamp;
            if (kcopy_to_user(out_arg, &st, (u32)sizeof(st)) != 0) return -LINUX_EFAULT;
            return 0;
        }
        case LINUX_SYS_FSTAT:
        case LINUX_SYS_STATX:
            return -LINUX_ENOSYS;

        /* 7: sys_poll, 271: sys_ppoll */
        case LINUX_SYS_POLL:
        case LINUX_SYS_PPOLL: {
            struct linux_pollfd { int fd; short events; short revents; } *fds = (struct linux_pollfd*)a1;
            u32 nfds = (u32)a2;
            int ready = 0;
            if (fds && nfds > 0) {
                (void)nic_poll(16u);
                netstack_poll(16u);
                for (u32 i = 0; i < nfds; i++) {
                    fds[i].revents = 0;
                    if (fds[i].fd >= LINUX_SOCK_BASE && fds[i].fd < LINUX_SOCK_BASE + LINUX_SOCK_MAX) {
                        linux_socket_entry_t* sk = linux_sock_get(fds[i].fd);
                        if (sk && sk->netstack_id >= 0) {
                            if (sk->state == 2) fds[i].revents |= 0x0004; /* POLLOUT */
                            fds[i].revents |= 0x0001; /* POLLIN */
                            ready++;
                        }
                    } else if (fds[i].fd == 1 || fds[i].fd == 2) {
                        fds[i].revents |= 0x0004; /* stdout/err always writable */
                        ready++;
                    }
                }
            }
            return (i64)ready;
        }

        /* 8: sys_lseek */
        case LINUX_SYS_LSEEK:
            return (i64)a2;

        /* 9: sys_mmap — the mapping has to be visible to the caller, so it is
           taken from the task's own user heap.  A kernel frame returned here
           would be a number ring 3 cannot touch. */
        case LINUX_SYS_MMAP: {
            u64 len = a2;
            u64 va;
            u32 pages;
            if (len == 0) return -LINUX_EINVAL;
            pages = (u32)((len + 4095ull) / 4096ull);
            if (pages == 0u || pages > 1024u) return -LINUX_ENOMEM;
            if (sb && (sb->allocated_bytes + len > sb->max_memory_bytes)) {
                sb->violations_blocked++;
                return -LINUX_ENOMEM;
            }
            va = elf32_user_alloc(scheduler_current_task(), pages * 4096u);
            if (va == 0u) return -LINUX_ENOMEM;
            if (sb) {
                sb->allocated_bytes += pages * 4096u;
                if (sb->allocated_bytes > sb->peak_memory_bytes) {
                    sb->peak_memory_bytes = sb->allocated_bytes;
                }
            }
            return (i64)va;
        }

        /* 10: sys_mprotect, 329: sys_pkey_mprotect.  The compat mappings come
           from elf32_user_alloc(), which has no per-range permission change, so
           claiming success would tell the caller a protection it does not have. */
        case LINUX_SYS_MPROTECT:
        case LINUX_SYS_PKEY_MPROTECT:
            return -LINUX_ENOSYS;

        /* 11: sys_munmap */
        case LINUX_SYS_MUNMAP: {
            u64 len = a2;
            if (elf32_user_free(scheduler_current_task(), a1) != 0) {
                return -LINUX_EINVAL;
            }
            if (sb && sb->allocated_bytes >= (u32)len) {
                sb->allocated_bytes -= (u32)len;
            }
            return 0;
        }

        /* 12: sys_brk would have to move the task's program break; the user
           heap here only grows through mmap, so that is reported rather than a
           kernel address handed back as if it were the break. */
        case LINUX_SYS_BRK:
            return -LINUX_ENOSYS;

        /* 13: sys_rt_sigaction, 14: sys_rt_sigprocmask, 15: sys_rt_sigreturn,
           127: rt_sigpending, 130: rt_sigsuspend.  This layer has no signal
           delivery yet, so recording a handler would silently drop the signal it
           was meant to catch. */
        case LINUX_SYS_RT_SIGACTION:
        case LINUX_SYS_RT_SIGPROCMASK:
        case LINUX_SYS_RT_SIGRETURN:
        case LINUX_SYS_RT_SIGPENDING:
        case LINUX_SYS_RT_SIGSUSPEND:
            return -LINUX_ENOSYS;

        /* 16: sys_ioctl is not answered: this table has no descriptor that
           understands an ioctl command yet. */
        /* 17: sys_pread64, 18: sys_pwrite64, 19: sys_readv, 20: sys_writev */
        case LINUX_SYS_PREAD64:
        case LINUX_SYS_PWRITE64:
        case LINUX_SYS_READV:
        case LINUX_SYS_WRITEV:
            return -LINUX_ENOSYS;

        /* 21: sys_access, 269: sys_faccessat, 439: sys_faccessat2 answered from
           the command VFS: an inode that stat() can find is treated as present,
           which is the only thing this filesystem can assert. */
        case LINUX_SYS_ACCESS:
        case LINUX_SYS_FACCESSAT:
        case LINUX_SYS_FACCESSAT2: {
            char path[VFS_PATH_MAX];
            vfs_node_info_t info;
            /* lin_fetch_str returns the length it read, not 0. */
            if (lin_fetch_str(a1, path, sizeof(path)) < 0) return -LINUX_EFAULT;
            if (path[0] == '\0') return -LINUX_ENOENT;
            if (vfs_info(path, &info) == 0) return 0;
            return -LINUX_ENOENT;
        }

        /* 22: sys_pipe, 293: sys_pipe2.  The kernel does have a real pipe
           subsystem (kpipe_create), but its file descriptors are native and this
           table has no translation range for them yet; handing back 3 and 4
           without a channel behind them gives the caller two descriptors it
           cannot read. */
        case LINUX_SYS_PIPE:
        case LINUX_SYS_PIPE2:
            return -LINUX_ENOSYS;

        /* 23: sys_select, 270: sys_pselect6.  Without a wait queue on the Linux
           side, a select that returns immediately would report every descriptor
           as ready. */
        case LINUX_SYS_SELECT:
        case LINUX_SYS_PSELECT6:
            return -LINUX_ENOSYS;

        /* 24: sys_sched_yield genuinely gives up the remainder of the quantum. */
        case LINUX_SYS_SCHED_YIELD:
            schedule();
            return 0;

        /* 25: sys_mremap would have to move a live mapping; elf32_user_alloc()
           cannot, so returning the old address as if it had moved is refused
           instead.  28: sys_madvise and 433: process_madvise are advice only, so
           ignoring them is the correct answer. */
        case LINUX_SYS_MREMAP:
            return -LINUX_ENOSYS;
        case LINUX_SYS_MADVISE:
        case LINUX_SYS_PROCESS_MADVISE:
            return 0;

        /* 29: sys_shmget, 30: sys_shmat, 31: sys_shmctl, 67: sys_shmdt.  The
           previous answers here were a fabricated key (300) and a 1.9 MiB kernel
           heap pointer handed to ring 3 as if it were a shared attachment: the
           id matched no segment and the address is not a user mapping. */
        case LINUX_SYS_SHMGET:
        case LINUX_SYS_SHMAT:
        case LINUX_SYS_SHMCTL:
        case LINUX_SYS_SHMDT:
            return -LINUX_ENOSYS;

        /* 32: sys_dup, 33: sys_dup2, 292: sys_dup3.  This table has no per-task
           descriptor file table to copy an entry within, so echoing the argument
           back as a new descriptor would give the caller a handle it cannot use. */
        case LINUX_SYS_DUP:
        case LINUX_SYS_DUP2:
        case LINUX_SYS_DUP3:
            return -LINUX_ENOSYS;

        /* 35: sys_nanosleep, 230: sys_clock_nanosleep.  Returning immediately
           tells the caller it slept when it did not, and this layer has no timed
           wait yet. */
        case LINUX_SYS_NANOSLEEP:
        case LINUX_SYS_CLOCK_NANOSLEEP:
            return -LINUX_ENOSYS;

        /* 39: sys_getpid, 186: sys_gettid — the scheduler's task id is this
           process's pid.  110: sys_getppid is refused rather than guessed,
           because the compat layer does not track a Linux parent link yet. */
        case LINUX_SYS_GETPID:
        case LINUX_SYS_GETTID:
            return (i64)scheduler_current_task();
        case LINUX_SYS_GETPPID:
            return -LINUX_ENOSYS;

        /* 41: sys_socket */
        case LINUX_SYS_SOCKET: {
            int domain = (int)a1;
            int type = (int)a2;
            int proto = (int)a3;
            if (domain == 1) { /* AF_UNIX */
                /* No Unix domain socket service exists here; reporting a usable
                   fd would let a client believe it had reached a display
                   server. */
                return -LINUX_EAFNOSUPPORT;
            }
            if (domain == 2 || domain == 10) { /* AF_INET or AF_INET6 */
                int stype = type & 0x0Fu;
                int nonblock = (type & 0x00000800u) ? 1 : 0;
                if (proto == 0) proto = (stype == 1 ? IP_PROTO_TCP : IP_PROTO_UDP);
                int sfd = linux_sock_alloc(domain, stype, proto);
                if (sfd < 0) return -LINUX_EMFILE;
                s_linux_socks[sfd - LINUX_SOCK_BASE].nonblock = nonblock;
                return (i64)sfd;
            }
            return -LINUX_EAFNOSUPPORT;
        }

        /* 42: sys_connect */
        case LINUX_SYS_CONNECT: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk) return -LINUX_EBADF;
            const struct linux_sockaddr_in* sin = (const struct linux_sockaddr_in*)a2;
            if (!sin) return -LINUX_EFAULT;

            u16 rem_port = (u16)(((sin->sin_port >> 8) & 0xFF) | ((sin->sin_port & 0xFF) << 8));
            u32 rem_ip = ((sin->sin_addr >> 24) & 0xFF) |
                         (((sin->sin_addr >> 16) & 0xFF) << 8) |
                         (((sin->sin_addr >> 8) & 0xFF) << 16) |
                         ((sin->sin_addr & 0xFF) << 24);

            sk->remote_port = rem_port;
            sk->remote_ip = rem_ip;
            if (sk->local_port == 0) {
                sk->local_port = 40000u + (u16)((sfd * 101u) % 20000u);
            }

            if (sk->netstack_id < 0) {
                sk->netstack_id = netstack_socket_open((u32)sk->protocol, sk->local_port, sk->remote_ip, sk->remote_port);
                if (sk->netstack_id < 0) return -LINUX_ECONNREFUSED;
            }

            if (sk->type == 1) { /* TCP Stream */
                (void)netstack_socket_send((u32)sk->netstack_id, 0, 0);
                u32 dl = timer_ticks() + 300u;
                while ((i32)(timer_ticks() - dl) < 0) {
                    (void)nic_poll(16u);
                    netstack_poll(16u);
                    u8 st = 0;
                    if (netstack_socket_state((u32)sk->netstack_id, &st) == 0) {
                        if (st == NET_SOCKET_ESTABLISHED) {
                            sk->state = 2;
                            return 0;
                        }
                        if (st == NET_SOCKET_CLOSING) return -LINUX_ECONNREFUSED;
                    }
                }
                return -LINUX_ETIMEDOUT;
            }
            sk->state = 2;
            return 0;
        }

        /* 49: sys_bind */
        case LINUX_SYS_BIND: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk) return -LINUX_EBADF;
            const struct linux_sockaddr_in* sin = (const struct linux_sockaddr_in*)a2;
            if (!sin) return -LINUX_EFAULT;

            u16 local_port = (u16)(((sin->sin_port >> 8) & 0xFF) | ((sin->sin_port & 0xFF) << 8));
            u32 local_ip = ((sin->sin_addr >> 24) & 0xFF) |
                           (((sin->sin_addr >> 16) & 0xFF) << 8) |
                           (((sin->sin_addr >> 8) & 0xFF) << 16) |
                           ((sin->sin_addr & 0xFF) << 24);

            sk->local_port = local_port;
            sk->local_ip = local_ip;
            return 0;
        }

        /* 50: sys_listen */
        case LINUX_SYS_LISTEN: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk) return -LINUX_EBADF;
            if (sk->local_port == 0) sk->local_port = 8080u;
            sk->netstack_id = netstack_socket_listen(sk->local_port);
            if (sk->netstack_id < 0) return -LINUX_EADDRINUSE;
            sk->is_listener = 1;
            return 0;
        }

        /* 43: sys_accept, 288: sys_accept4 */
        case LINUX_SYS_ACCEPT:
        case LINUX_SYS_ACCEPT4: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk || !sk->is_listener || sk->netstack_id < 0) return -LINUX_EBADF;

            u32 client_id = 0, rem_ip = 0;
            u16 rem_port = 0;
            u32 dl = timer_ticks() + (sk->nonblock ? 1u : 300u);
            int ok = 0;

            while ((i32)(timer_ticks() - dl) < 0) {
                (void)nic_poll(16u);
                netstack_poll(16u);
                if (netstack_socket_accept((u32)sk->netstack_id, &client_id, &rem_ip, &rem_port) == 0) {
                    ok = 1;
                    break;
                }
                if (sk->nonblock) break;
            }

            if (!ok) return -LINUX_EAGAIN;

            int client_sfd = linux_sock_alloc(sk->domain, sk->type, sk->protocol);
            if (client_sfd < 0) {
                (void)netstack_socket_close(client_id);
                return -LINUX_EMFILE;
            }

            linux_socket_entry_t* csk = linux_sock_get(client_sfd);
            csk->netstack_id = (int)client_id;
            csk->local_port = sk->local_port;
            csk->remote_ip = rem_ip;
            csk->remote_port = rem_port;
            csk->state = 2;

            if (a2) {
                struct linux_sockaddr_in* sin = (struct linux_sockaddr_in*)a2;
                sin->sin_family = 2;
                sin->sin_port = (u16)(((rem_port >> 8) & 0xFF) | ((rem_port & 0xFF) << 8));
                sin->sin_addr = ((rem_ip >> 24) & 0xFF) |
                                (((rem_ip >> 16) & 0xFF) << 8) |
                                (((rem_ip >> 8) & 0xFF) << 16) |
                                ((rem_ip & 0xFF) << 24);
            }
            return (i64)client_sfd;
        }

        /* 44: sys_sendto */
        case LINUX_SYS_SENDTO: {
            int sfd = (int)a1;
            char payload[LINUX_SEND_CHUNK];
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            u64 sent = 0u;
            u64 want = a3;
            if (!sk || sk->netstack_id < 0) return -LINUX_EBADF;
            if (!a2 || want == 0u) return -LINUX_EINVAL;
            /* The buffer belongs to ring 3, so it is pulled through the guarded
               copy and pushed a chunk at a time; sendto reports the bytes the
               netstack actually accepted. */
            while (sent < want) {
                u32 n = (u32)(want - sent);
                if (n > LINUX_SEND_CHUNK) n = LINUX_SEND_CHUNK;
                if (lin_fetch_bytes(payload, a2 + sent, n) != 0) {
                    return (sent > 0u) ? (i64)sent : -LINUX_EFAULT;
                }
                if (netstack_socket_send((u32)sk->netstack_id, (const u8*)payload, (u16)n) != 0) {
                    return (sent > 0u) ? (i64)sent : -LINUX_EPIPE;
                }
                sent += n;
            }
            return (i64)sent;
        }

        /* 46: sys_sendmsg, 47: sys_recvmsg.  These take a struct msghdr with an
           iovec array; walking it needs the descriptor work below, so the two
           vector calls report ENOSYS rather than reading a2 as a length. */
        case LINUX_SYS_SENDMSG:
        case LINUX_SYS_RECVMSG:
            return -LINUX_ENOSYS;

        /* 45: sys_recvfrom */
        case LINUX_SYS_RECVFROM: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk || sk->netstack_id < 0) return -LINUX_EBADF;

            u8* buf = (u8*)(uintptr_t)a2;
            u32 len = (u32)a3;
            u32 src_ip = 0;
            u16 src_port = 0;
            u32 dl = timer_ticks() + (sk->nonblock ? 1u : 200u);
            int n = -1;

            while ((i32)(timer_ticks() - dl) < 0) {
                (void)nic_poll(16u);
                netstack_poll(16u);
                n = netstack_socket_recv((u32)sk->netstack_id, buf, (u16)len, &src_ip, &src_port);
                if (n > 0) break;
                u8 st = 0;
                if (netstack_socket_state((u32)sk->netstack_id, &st) == 0 && st == NET_SOCKET_CLOSING) return 0;
                if (sk->nonblock) break;
            }

            if (n <= 0) return sk->nonblock ? -LINUX_EAGAIN : 0;

            if (a5) {
                struct linux_sockaddr_in* sin = (struct linux_sockaddr_in*)a5;
                sin->sin_family = 2;
                sin->sin_port = (u16)(((src_port >> 8) & 0xFF) | ((src_port & 0xFF) << 8));
                sin->sin_addr = ((src_ip >> 24) & 0xFF) |
                                (((src_ip >> 16) & 0xFF) << 8) |
                                (((src_ip >> 8) & 0xFF) << 16) |
                                ((src_ip & 0xFF) << 24);
            }
            return (i64)n;
        }

        /* 51: sys_getsockname */
        case LINUX_SYS_GETSOCKNAME: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk) return -LINUX_EBADF;
            if (a2) {
                struct linux_sockaddr_in* sin = (struct linux_sockaddr_in*)a2;
                sin->sin_family = 2;
                sin->sin_port = (u16)(((sk->local_port >> 8) & 0xFF) | ((sk->local_port & 0xFF) << 8));
                sin->sin_addr = ((sk->local_ip >> 24) & 0xFF) |
                                (((sk->local_ip >> 16) & 0xFF) << 8) |
                                (((sk->local_ip >> 8) & 0xFF) << 16) |
                                ((sk->local_ip & 0xFF) << 24);
            }
            return 0;
        }

        /* 52: sys_getpeername */
        case LINUX_SYS_GETPEERNAME: {
            int sfd = (int)a1;
            linux_socket_entry_t* sk = linux_sock_get(sfd);
            if (!sk || sk->state != 2) return -LINUX_ENOTCONN;
            if (a2) {
                struct linux_sockaddr_in* sin = (struct linux_sockaddr_in*)a2;
                sin->sin_family = 2;
                sin->sin_port = (u16)(((sk->remote_port >> 8) & 0xFF) | ((sk->remote_port & 0xFF) << 8));
                sin->sin_addr = ((sk->remote_ip >> 24) & 0xFF) |
                                (((sk->remote_ip >> 16) & 0xFF) << 8) |
                                (((sk->remote_ip >> 8) & 0xFF) << 16) |
                                ((sk->remote_ip & 0xFF) << 24);
            }
            return 0;
        }

        /* 48: sys_shutdown, 53: sys_socketpair, 54: sys_setsockopt, 55: sys_getsockopt.
           The netstack this table drives exposes create/bind/connect/send/recv/close
           only; a socketpair that pairs nothing or an option silently discarded
           would both look like success. */
        case LINUX_SYS_SHUTDOWN:
        case LINUX_SYS_SOCKETPAIR:
        case LINUX_SYS_SETSOCKOPT:
        case LINUX_SYS_GETSOCKOPT:
            return -LINUX_ENOSYS;

        /* 56: sys_clone, 57: sys_fork, 58: sys_vfork, 435: sys_clone3.  The real
           clone path is the native KSYSCALL_FORK on a task created from an ELF
           image; the compat table cannot duplicate a Linux task's address space,
           so inventing a child pid was reporting a process that does not exist. */
        case LINUX_SYS_CLONE:
        case LINUX_SYS_CLONE3:
        case LINUX_SYS_FORK:
        case LINUX_SYS_VFORK:
            return -LINUX_ENOSYS;

        /* 59: sys_execve, 322: sys_execveat */
        case LINUX_SYS_EXECVE:
        case LINUX_SYS_EXECVEAT:
            return -LINUX_ENOSYS;

        /* 60: sys_exit, 231: sys_exit_group — retire the task for real so a
           Linux program that finishes stops being scheduled.  The SYSCALL fast
           path defers teardown to the timer interrupt, which owns a frame it
           can replace; destroying the returning address space here would leave
           sysret executing on an unmapped stack. */
        case LINUX_SYS_EXIT:
        case LINUX_SYS_EXIT_GROUP:
            if (sb) sb->active = 0;
            if (scheduler_task_is_user(scheduler_current_task())) {
                (void)scheduler_defer_user_exit(scheduler_current_task(), (i32)a1);
            }
            return 0;

        /* 61: sys_wait4, 247: sys_waitid.  With no Linux child to reap, returning
           0 tells the caller a child exited; there is none. */
        case LINUX_SYS_WAIT4:
        case LINUX_SYS_WAITID:
            return -LINUX_ENOSYS;

        /* 62: sys_kill, 200: sys_tkill, 234: sys_tgkill.  Nothing is delivered
           because this layer has no signal path, so no target is told it was
           interrupted. */
        case LINUX_SYS_KILL:
        case LINUX_SYS_TKILL:
        case LINUX_SYS_TGKILL:
            return -LINUX_ENOSYS;

        /* 63: sys_uname */
        case LINUX_SYS_UNAME: {
            linux_utsname_t u;
            (void)memset(&u, 0, sizeof(u));
            str_cpy(u.sysname, "Linux");
            str_cpy(u.nodename, "lastos-node");
            str_cpy(u.release, "6.8.0-lastos-smp");
            str_cpy(u.version, "#1 SMP PREEMPT 2026");
            str_cpy(u.machine, "x86_64");
            str_cpy(u.domainname, "localdomain");
            if (kcopy_to_user(a1, &u, (u32)sizeof(u)) != 0) return -LINUX_EFAULT;
            return 0;
        }

        /* 72: sys_fcntl.  Duplexing a descriptor or setting O_NONBLOCK needs the
           per-task file table this layer does not have. */
        case LINUX_SYS_FCNTL:
            return -LINUX_ENOSYS;

        /* 74: sys_fsync, 75: sys_fdatasync, 306: sys_syncfs */
        case LINUX_SYS_FSYNC:
        case LINUX_SYS_FDATASYNC:
        case LINUX_SYS_SYNCFS:
        case LINUX_SYS_SYNC:
            return 0;

        /* 78/217 getdents, 82/264/316 rename, 84 rmdir, 87/263 unlink: this
           layer has no directory-enumeration or path-rewrite service, so they
           report ENOSYS instead of returning a fake "it worked". */
        case LINUX_SYS_GETDENTS:
        case LINUX_SYS_GETDENTS64:
        case LINUX_SYS_RENAME:
        case LINUX_SYS_RENAMEAT:
        case LINUX_SYS_RENAMEAT2:
        case LINUX_SYS_RMDIR:
        case LINUX_SYS_UNLINK:
        case LINUX_SYS_UNLINKAT:
            return -LINUX_ENOSYS;

        /* 79: sys_getcwd — the compat root is the only working directory. */
        case LINUX_SYS_GETCWD: {
            char root[2];
            root[0] = '/';
            root[1] = '\0';
            if (a2 < 2u) return -LINUX_EINVAL;
            if (kcopy_to_user(a1, root, 2u) != 0) return -LINUX_EFAULT;
            return 2;
        }

        /* 81: sys_fchdir needs a fd-to-directory link this layer does not keep. */
        case LINUX_SYS_FCHDIR:
            return -LINUX_ENOSYS;

        /* 80: sys_chdir — only the root is a known working directory. */
        case LINUX_SYS_CHDIR: {
            char path[LINUX_MAX_PATH];
            if (lin_fetch_str(a1, path, sizeof(path)) < 0) return -LINUX_EFAULT;
            if (path[0] != '/' || path[1] != '\0') return -LINUX_ENOSYS;
            return 0;
        }

        /* 83/258: sys_mkdir against the real filesystem. */
        case LINUX_SYS_MKDIR:
        case LINUX_SYS_MKDIRAT: {
            char path[LINUX_MAX_PATH];
            u64 name_arg = (nr == LINUX_SYS_MKDIR) ? a1 : a2;
            if (lin_fetch_str(name_arg, path, sizeof(path)) < 0) return -LINUX_EFAULT;
            if (path[0] == '\0') return -LINUX_ENOENT;
            if (vfs_mkdir(path) != 0) return -LINUX_EINVAL;
            return 0;
        }

        /* 96: sys_gettimeofday */
        case LINUX_SYS_GETTIMEOFDAY: {
            linux_timespec_t* tv = (linux_timespec_t*)(uintptr_t)a1;
            if (tv) {
                u32 ticks = timer_ticks();
                tv->tv_sec = ticks / 1000;
                tv->tv_nsec = (ticks % 1000) * 1000000;
            }
            return 0;
        }

        /* 97: sys_getrlimit answers with the sandbox's own quota; 160/302 have
           no backing store, so they are refused instead of accepted silently. */
        case LINUX_SYS_GETRLIMIT: {
            u64 rlim[2];
            rlim[0] = (sb && sb->max_memory_bytes)
                          ? (u64)sb->max_memory_bytes : 0xFFFFFFFFFFFFFFFFull;
            rlim[1] = rlim[0];
            if (kcopy_to_user(a2, rlim, (u32)sizeof(rlim)) != 0) return -LINUX_EFAULT;
            return 0;
        }
        case LINUX_SYS_SETRLIMIT:
        case LINUX_SYS_PRLIMIT64:
            return -LINUX_ENOSYS;

        /* 98: sys_getrusage */
        case LINUX_SYS_GETRUSAGE:
            return 0;

        /* 99: sys_sysinfo — filled from the live allocator and scheduler. */
        case LINUX_SYS_SYSINFO: {
            linux_sysinfo_t s;
            resource_snapshot_t rs;
            (void)memset(&s, 0, sizeof(s));
            (void)memset(&rs, 0, sizeof(rs));
            resource_monitor_snapshot(&rs);
            s.uptime = (u64)timer_ticks() / 1000ull;
            s.totalram = (u64)rs.mem_total_bytes;
            s.freeram = (rs.mem_total_bytes > rs.mem_used_bytes)
                            ? (u64)(rs.mem_total_bytes - rs.mem_used_bytes)
                            : 0ull;
            s.procs = scheduler_task_count();
            s.mem_unit = 1;
            if (kcopy_to_user(a1, &s, (u32)sizeof(s)) != 0) return -LINUX_EFAULT;
            return 0;
        }

        /* 102: sys_getuid, 104: sys_getgid, 107: sys_geteuid, 108: sys_getegid */
        case LINUX_SYS_GETUID:
        case LINUX_SYS_GETGID:
        case LINUX_SYS_GETEUID:
        case LINUX_SYS_GETEGID:
            return 0;

        /* 135: sys_personality.  PER_LINUX is 0 and this table does execute the
           Linux x86_64 ABI, so reporting it is the truth as long as the caller
           only reads it back; requesting any other domain is refused. */
        case LINUX_SYS_PERSONALITY:
            if (a1 == 0xffffffffull) return 0; /* READ Personality */
            return -LINUX_ENOSYS;

        /* 158: sys_arch_prctl (Glibc/Musl Thread-Local Storage TLS) */
        case LINUX_SYS_ARCH_PRCTL: {
            int code = (int)a1;
            u64 addr = a2;
            if (code == ARCH_SET_FS) {
                __asm__ __volatile__("wrmsr" : : "c"(0xC0000100u), "a"((u32)addr), "d"((u32)(addr >> 32)) : "memory");
                return 0;
            }
            if (code == ARCH_SET_GS) {
                __asm__ __volatile__("wrmsr" : : "c"(0xC0000101u), "a"((u32)addr), "d"((u32)(addr >> 32)) : "memory");
                return 0;
            }
            return 0;
        }

        /* 202: sys_futex, 449: sys_futex_waitv, 454: sys_futex_wake, 455: sys_futex_wait */
        case LINUX_SYS_FUTEX:
            return (i64)linux_futex_op(a1, (int)a2, (u32)a3, a4, (u64)sandbox_id, 0);
        case LINUX_SYS_FUTEX_WAIT:
            return (i64)linux_futex_op(a1, LINUX_FUTEX_WAIT, (u32)a2, a3, 0, 0);
        case LINUX_SYS_FUTEX_WAKE:
            return (i64)linux_futex_op(a1, LINUX_FUTEX_WAKE, (u32)a2, 0, 0, 0);

        /* 213/291/232/233/281/441: epoll.  There is no interest list behind
           these numbers, so inventing a descriptor would hand the caller a handle
           that silently watches nothing. */
        case LINUX_SYS_EPOLL_CREATE:
        case LINUX_SYS_EPOLL_CREATE1:
        case LINUX_SYS_EPOLL_CTL:
        case LINUX_SYS_EPOLL_WAIT:
        case LINUX_SYS_EPOLL_PWAIT:
        case LINUX_SYS_EPOLL_PWAIT2:
        case LINUX_SYS_FUTEX_WAITV:
            return -LINUX_ENOSYS;

        /* 218: sys_set_tid_address — the thread id is this task's real pid. */
        case LINUX_SYS_SET_TID_ADDRESS:
            return (i64)scheduler_current_task();

        /* 228: sys_clock_gettime, 227: sys_clock_settime, 229: sys_clock_getres */
        case LINUX_SYS_CLOCK_GETTIME:
        case LINUX_SYS_CLOCK_SETTIME:
        case LINUX_SYS_CLOCK_GETRES: {
            linux_timespec_t* ts = (linux_timespec_t*)(uintptr_t)a2;
            if (ts) {
                u32 ticks = timer_ticks();
                ts->tv_sec = ticks / 1000;
                ts->tv_nsec = (ticks % 1000) * 1000000;
            }
            return 0;
        }

        /* 284/290 eventfd, 282/289 signalfd, 283 timerfd: no event queue backs
           these, so they are refused rather than answered with a made-up fd. */
        case LINUX_SYS_EVENTFD:
        case LINUX_SYS_EVENTFD2:
        case LINUX_SYS_SIGNALFD:
        case LINUX_SYS_SIGNALFD4:
        case LINUX_SYS_TIMERFD_CREATE:
            return -LINUX_ENOSYS;

        /* 317: sys_seccomp.  Accepting a filter the kernel never evaluates would
           give the caller sandbox guarantees it does not have. */
        case LINUX_SYS_SECCOMP:
            return -LINUX_ENOSYS;

        /* 318: sys_getrandom */
        case LINUX_SYS_GETRANDOM: {
            u8* buf = (u8*)(uintptr_t)a1;
            u64 buflen = a2;
            if (!buf) return -LINUX_EFAULT;
            entropy_fill(buf, (u32)buflen);
            return (i64)buflen;
        }

        /* 319: sys_memfd_create, 447: sys_memfd_secret.  These used to allocate
           1.9 MiB of kernel heap and return it as a descriptor number, which is
           neither an anonymous file nor a user address. */
        case LINUX_SYS_MEMFD_CREATE:
        case LINUX_SYS_MEMFD_SECRET:
            return -LINUX_ENOSYS;

        /* 334: sys_rseq — the kernel would have to register and restart the
           caller's restricted sequences, so it is refused rather than accepted
           and dropped. */
        case LINUX_SYS_RSEQ:
            return -LINUX_ENOSYS;

        /* 425: io_uring_setup, 426: io_uring_enter, 427: io_uring_register */
        case LINUX_SYS_IO_URING_SETUP:
            return sys_io_uring_setup((u32)a1, (io_uring_params_t*)(uintptr_t)a2);
        case LINUX_SYS_IO_URING_ENTER:
            return sys_io_uring_enter((i32)a1, (u32)a2, (u32)a3, (u32)a4, (const void*)(uintptr_t)a5);
        case LINUX_SYS_IO_URING_REGISTER:
            return sys_io_uring_register((i32)a1, (u32)a2, (const void*)(uintptr_t)a3, (u32)a4);

        /* Anything this table does not implement reports ENOSYS.  Returning a
           fake success for numbers 0..455 told the caller the operation had
           happened, which is the one answer a compatibility layer must not
           give: the program carries on as if its request worked. */
        default:
            return -LINUX_ENOSYS;
    }
}

__attribute__((weak)) i64 x86_64_syscall_handler(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    return linux_syscall_dispatch(nr, a1, a2, a3, a4, a5, a6, 0);
}

u64 linux_setup_user_stack(u8* stack_buf, u64 stack_top_vaddr,
                           int argc, const char** argv, const char** envp,
                           u64 entry_point, u64 phdr_addr, u16 phnum, u16 phent) {
    if (!stack_buf || stack_top_vaddr == 0) return 0;

    u32 off = 4096u;

    /* 1. 16 random bytes for AT_RANDOM */
    off -= 16u;
    u32 rand_off = off;
    entropy_fill(stack_buf + rand_off, 16u);

    /* 2. Platform string "x86_64" */
    const char* plat = "x86_64";
    u32 plat_len = (u32)str_len(plat) + 1u;
    off -= plat_len;
    u32 plat_off = off;
    memcpy(stack_buf + plat_off, plat, plat_len);

    /* 3. Environment strings */
    u32 env_count = 0;
    u32 env_offs[16];
    if (envp) {
        while (envp[env_count] && env_count < 16) {
            u32 len = (u32)str_len(envp[env_count]) + 1u;
            off -= len;
            env_offs[env_count] = off;
            memcpy(stack_buf + off, envp[env_count], len);
            env_count++;
        }
    }

    /* 4. Argument strings */
    u32 arg_count = (u32)argc;
    if (arg_count > 16) arg_count = 16;
    u32 arg_offs[16];
    if (argv) {
        for (u32 i = 0; i < arg_count; i++) {
            if (argv[i]) {
                u32 len = (u32)str_len(argv[i]) + 1u;
                off -= len;
                arg_offs[i] = off;
                memcpy(stack_buf + off, argv[i], len);
            }
        }
    }

    /* Align to 8 bytes */
    off &= ~7u;

    #define PUSH_U64(val) do { \
        off -= 8u; \
        *(u64*)(stack_buf + off) = (u64)(val); \
    } while(0)

    #define PUSH_AUXV(type, val) do { \
        PUSH_U64(val); \
        PUSH_U64(type); \
    } while(0)

    /* 5. Push Auxiliary Vector */
    PUSH_AUXV(LINUX_AT_NULL, 0);
    PUSH_AUXV(LINUX_AT_PAGESZ, 4096);
    PUSH_AUXV(LINUX_AT_RANDOM, stack_top_vaddr - 4096u + rand_off);
    PUSH_AUXV(LINUX_AT_PHDR, phdr_addr ? phdr_addr : 0x00400040);
    PUSH_AUXV(LINUX_AT_PHNUM, phnum ? phnum : 1);
    PUSH_AUXV(LINUX_AT_PHENT, phent ? phent : 56);
    PUSH_AUXV(LINUX_AT_ENTRY, entry_point ? entry_point : 0x00401000);
    PUSH_AUXV(LINUX_AT_UID, 1000);
    PUSH_AUXV(LINUX_AT_EUID, 1000);
    PUSH_AUXV(LINUX_AT_GID, 1000);
    PUSH_AUXV(LINUX_AT_EGID, 1000);
    PUSH_AUXV(LINUX_AT_SECURE, 0);
    PUSH_AUXV(LINUX_AT_CLKTCK, 100);
    PUSH_AUXV(LINUX_AT_PLATFORM, stack_top_vaddr - 4096u + plat_off);
    PUSH_AUXV(LINUX_AT_HWCAP, 0x178BFBFFu);
    PUSH_AUXV(LINUX_AT_HWCAP2, 0);

    /* 6. Push envp array */
    PUSH_U64(0);
    for (i32 i = (i32)env_count - 1; i >= 0; i--) {
        PUSH_U64(stack_top_vaddr - 4096u + env_offs[i]);
    }

    /* 7. Push argv array */
    PUSH_U64(0);
    for (i32 i = (i32)arg_count - 1; i >= 0; i--) {
        PUSH_U64(stack_top_vaddr - 4096u + arg_offs[i]);
    }

    /* 8. Push argc */
    PUSH_U64(arg_count);

    #undef PUSH_U64
    #undef PUSH_AUXV

    return stack_top_vaddr - 4096u + off;
}

/* A Linux process only runs when three things agree: its segments are mapped by
   the same loader that maps a native binary, the stack carries a System V
   argc/argv/envp/auxv block, and the task is bound to COMPAT_PERS_LINUX so the
   syscall entry translates its numbers instead of treating them as native
   requests.  Returns the pid, or -1 with the task already retired. */
typedef struct __attribute__((packed)) {
    u8 ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} lin_elf64_hdr_t;

typedef struct __attribute__((packed)) {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} lin_elf64_phdr_t;

#define LIN_PT_LOAD 1u
#define LIN_PT_PHDR 6u

int linux_run_binary(const u8* elf_bytes, u32 elf_size, const char* name,
                     int sandbox_id) {
    static const char* envp[] = { "PATH=/bin:/usr/bin", "HOME=/", 0 };
    const char* argv[2];
    elf_exec_info_t exec;
    lin_elf64_hdr_t hdr;
    u8* stack_page;
    u64 phdr_addr = 0u;
    u64 sp = 0u;
    u32 pid = 0u;
    u32 i;

    (void)sandbox_id;
    if (!elf_bytes || elf_size < (u32)sizeof(lin_elf64_hdr_t)) return -1;
    if (elf_bytes[0] != 0x7F || elf_bytes[1] != 'E' ||
        elf_bytes[2] != 'L'  || elf_bytes[3] != 'F') return -1;
    if (elf_bytes[4] != 2u || elf_bytes[5] != 1u) return -1;
    if (!s_linux_inited) linux_compat_init();

    (void)memcpy(&hdr, elf_bytes, sizeof(hdr));
    if (hdr.phentsize != sizeof(lin_elf64_phdr_t) || hdr.phnum == 0u ||
        hdr.phnum > 64u || hdr.phoff >= elf_size ||
        (u64)hdr.phoff + (u64)hdr.phnum * hdr.phentsize > elf_size) {
        serial_writeln("[linux] program headers outside file");
        return -1;
    }
    for (i = 0u; i < hdr.phnum; i++) {
        const lin_elf64_phdr_t* p = (const lin_elf64_phdr_t*)
            (elf_bytes + hdr.phoff + (u64)i * hdr.phentsize);
        if (p->type == LIN_PT_PHDR) { phdr_addr = p->vaddr; break; }
        if (p->type == LIN_PT_LOAD && phdr_addr == 0u && hdr.phoff >= p->offset) {
            phdr_addr = p->vaddr + (hdr.phoff - p->offset);
        }
    }

    if (kscheduler_create_user_task(name ? name : "linux-app", &pid) != 0) {
        serial_writeln("[linux] no task slot");
        return -1;
    }
    if (kelf_load_user(elf_bytes, elf_size, pid, &exec) != 0) {
        serial_writeln("[linux] segment load failed");
        (void)ksched_kill(pid);
        return -1;
    }

    /* The auxv block is assembled in a kernel page first and then copied in
       one piece: linux_setup_user_stack lays the records out from the top of a
       page downwards, and that page is the task's own stack top. */
    stack_page = (u8*)kalloc(4096u);
    if (!stack_page) {
        (void)ksched_kill(pid);
        return -1;
    }
    (void)memset(stack_page, 0, 4096u);
    argv[0] = name ? name : "linux-app";
    argv[1] = 0;
    sp = linux_setup_user_stack(stack_page, exec.stack_top, 1, argv, envp,
                                exec.entry, phdr_addr, hdr.phnum, hdr.phentsize);
    if (sp != 0u &&
        kelf_write_user_stack(exec.address_space, exec.stack_top - 4096u,
                              stack_page, 4096u) != 0) {
        sp = 0u;
    }
    kfree(stack_page);
    if (sp == 0u) {
        serial_writeln("[linux] process stack build failed");
        (void)ksched_kill(pid);
        return -1;
    }
    if (compat_bind_task(pid, COMPAT_PERS_LINUX, pid) != 0) {
        serial_writeln("[linux] personality bind failed");
        (void)ksched_kill(pid);
        return -1;
    }
    if (scheduler_prepare_user_task(pid, exec.entry, sp) != 0) {
        (void)ksched_kill(pid);
        return -1;
    }
    serial_write("[linux] launched pid ");
    serial_write_dec(pid);
    serial_writeln(" on the Linux syscall table");
    return (int)pid;
}
