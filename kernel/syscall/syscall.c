/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: syscall.c
 * Purpose: Kernel System Call Dispatcher, Ring 3 Interface & Security Boundary
 * =============================================================================
 */

#include "syscall.h"
#include "kernel_api.h"
#include "scheduler.h"
#include "audit.h"
#include "ktrace.h"
#include "string.h"
#include "spinlock.h"
#include "netstack.h"
#include "elf_loader.h"
#include "paging.h"
#include "gfx.h"
#include "interrupts.h"
#include "keyboard.h"
#include "kconsole.h"
#include "xhci.h"
#include "mouse.h"
#include "heap.h"
#include "security.h"
#include "pipe.h"
#include "pty.h"
#include "vma.h"
#include "hpet.h"
#include "timer.h"
#include "kshield.h"
#include "serial.h"


#define PIPE_MAX 64u
#define PIPE_CAPACITY 4096u

/* Largest user ELF the EXEC/SPAWN syscalls will stage in the kernel heap.
   Upgraded to 16 MiB to support full-featured userland applications and runtimes. */
#define USER_ELF_MAX_SIZE (16u * 1024u * 1024u)

typedef struct {
    u8 used;
    u8 closed;
    u16 reserved;
    u32 owner_task;
    u32 read_pos;
    u32 write_pos;
    u32 count;
    u8 data[PIPE_CAPACITY];
} syscall_pipe_t;

static syscall_pipe_t g_pipes[PIPE_MAX];
static u32 g_gfx_owner_task = 0u;
static u32 g_gfx_blits_presented = 0u;
static spinlock_t g_gfx_lock;
static spinlock_t g_pipe_lock;


static int gfx_claim(u32 task_id) {
    /* SECURITY: spinlock prevents TOCTOU race on SMP where two CPUs
     * could both see g_gfx_owner_task == 0 and claim ownership. */
    spinlock_lock(&g_gfx_lock);
    if (g_gfx_owner_task == 0u || g_gfx_owner_task == task_id) {
        g_gfx_owner_task = task_id;
        spinlock_unlock(&g_gfx_lock);
        return 0;
    }
    spinlock_unlock(&g_gfx_lock);
    return -1;
}

static u32 gfx_enter_kernel_space(void) {
    u32 previous = paging_space_current();
    if (previous != PAGING_SPACE_KERNEL) (void)paging_space_switch(PAGING_SPACE_KERNEL);
    return previous;
}

static void gfx_leave_kernel_space(u32 previous) {
    if (previous != PAGING_SPACE_KERNEL) (void)paging_space_switch(previous);
}

static int pipe_index(u32 id) {
    if (id == 0u || id > PIPE_MAX || !g_pipes[id - 1u].used) return -1;
    return (int)(id - 1u);
}

static int pipe_access_allowed(const syscall_pipe_t* pipe, u32 task_id) {
    if (!pipe || !pipe->used) return 0;
    return pipe->owner_task == task_id ||
           kproc_is_descendant(task_id, pipe->owner_task);
}

void syscall_release_task(u32 task_id) {
    kfile_release_task(task_id);
    knetstack_socket_release_task(task_id);
    for (u32 i = 0u; i < PIPE_MAX; i++) {
        if (g_pipes[i].used && g_pipes[i].owner_task == task_id) {
            memset(&g_pipes[i], 0, sizeof(g_pipes[i]));
        }
    }
    if (g_gfx_owner_task == task_id) {
        /* A graphics client may terminate through EXIT; tear down the video
           mode from the kernel lifecycle path, not from a dying user frame.
           Use gfx_restore_text(): gfx_exit_graphics is a no-op on VBE/BGA
           and would leave the desktop's framebuffer active with no way back
           to a visible VGA text console.  The caller may still be running in
           the user address space (EXIT tears down the dying frame after we
           return), and gfx_restore_text touches VGA plane memory, so switch
           to the kernel space for the call. */
        u32 previous = gfx_enter_kernel_space();
        gfx_restore_text();
        gfx_leave_kernel_space(previous);
        g_gfx_owner_task = 0u;
    }
}

static u32 syscall_fork_user(registers_t* regs, u32 parent_task) {
    u32 child_pid = 0u;
    elf_exec_info_t exec;
    if (!regs || parent_task == 0u || regs->cs != 0x1Bu || regs->ss != 0x23u) {
        return (u32)-1;
    }
    if (kscheduler_create_user_task("fork-child", &child_pid) != 0) return (u32)-1;
    if (kelf_clone_user(parent_task, child_pid, &exec) != 0 ||
        scheduler_prepare_fork_task(child_pid, regs, exec.address_space) != 0) {
        (void)ksched_kill(child_pid);
        return (u32)-1;
    }
    return child_pid;
}

static int syscall_socket_allowed(u32 socket_id, u32 task_id) {
    u32 owner = netstack_socket_owner(socket_id);
    return owner == 0u || owner == task_id || kproc_is_descendant(task_id, owner);
}

#define SYSCALL_ABI_VERSION 0x00010000u

static u32 g_syscall_count = 0u;
static u32 g_syscall_faults = 0u;
static u8 g_policy_active[SYSCALL_POLICY_MAX_TASKS];
static u8 g_policy_allow[SYSCALL_POLICY_MAX_TASKS][SYSCALL_POLICY_MAX_NR / 8u];
static u32 g_policy_denied[SYSCALL_POLICY_MAX_TASKS];
static u32 g_policy_task_id[SYSCALL_POLICY_MAX_TASKS];
static spinlock_t g_exec_lock;
static u8 g_policy_task_used[SYSCALL_POLICY_MAX_TASKS];

static int policy_slot(u32 task_id, int create) {
    int free_slot = -1;
    for (u32 i = 0u; i < SYSCALL_POLICY_MAX_TASKS; i++) {
        if (g_policy_task_used[i] && g_policy_task_id[i] == task_id) return (int)i;
        if (!g_policy_task_used[i] && free_slot < 0) free_slot = (int)i;
    }
    if (create && free_slot >= 0) {
        g_policy_task_used[(u32)free_slot] = 1u;
        g_policy_task_id[(u32)free_slot] = task_id;
        return free_slot;
    }
    return -1;
}

void syscall_policy_reset(u32 task_id) {
    int slot = policy_slot(task_id, 1);
    if (slot < 0) return;
    g_policy_active[(u32)slot] = 0u;
    g_policy_denied[(u32)slot] = 0u;
    memset(g_policy_allow[(u32)slot], 0xFF, sizeof(g_policy_allow[(u32)slot]));
}

int syscall_policy_set(u32 task_id, u32 nr, u8 allowed) {
    int slot;
    if (nr >= SYSCALL_POLICY_MAX_NR) return -1;
    slot = policy_slot(task_id, 1);
    if (slot < 0) return -1;
    g_policy_active[(u32)slot] = 1u;
    if (allowed) {
        g_policy_allow[(u32)slot][nr / 8u] |= (u8)(1u << (nr % 8u));
    } else {
        g_policy_allow[(u32)slot][nr / 8u] &= (u8)~(1u << (nr % 8u));
    }
    return 0;
}

int syscall_policy_allowed(u32 task_id, u32 nr) {
    int slot;
    if (nr >= SYSCALL_POLICY_MAX_NR) return 0;
    slot = policy_slot(task_id, 1);
    if (slot < 0 || !g_policy_active[(u32)slot]) return 1;
    return (g_policy_allow[(u32)slot][nr / 8u] & (u8)(1u << (nr % 8u))) != 0u;
}

u32 syscall_policy_denied_count(u32 task_id) {
    int slot = policy_slot(task_id, 0);
    return slot >= 0 ? g_policy_denied[(u32)slot] : 0u;
}

static int copy_user_str(u64 user_ptr, char* out, u32 max_len) {
    if (!out || max_len == 0u || user_ptr == 0u) return -1;
    for (u32 i = 0; i < max_len; i++) {
        u8 ch = 0u;
        if (kcopy_from_user(&ch, user_ptr + i, 1u) != 0) {
            return -1;
        }
        out[i] = (char)ch;
        if (out[i] == '\0') {
            return 0;
        }
    }
    out[max_len - 1u] = '\0';
    return 0;
}

static int syscall_copy_path(u64 user_ptr, char* out, u32 out_len) {
    return copy_user_str(user_ptr, out, out_len);
}

void syscall_init(void) {
    g_syscall_count = 0u;
    g_syscall_faults = 0u;
    memset(g_policy_active, 0, sizeof(g_policy_active));
    memset(g_policy_denied, 0, sizeof(g_policy_denied));
    memset(g_policy_task_id, 0, sizeof(g_policy_task_id));
    memset(g_policy_task_used, 0, sizeof(g_policy_task_used));
    memset(g_policy_allow, 0xFF, sizeof(g_policy_allow));
    memset(g_pipes, 0, sizeof(g_pipes));
    g_gfx_owner_task = 0u;
    spinlock_init(&g_exec_lock);
    spinlock_init(&g_gfx_lock);
    spinlock_init(&g_pipe_lock);
}

u32 syscall_version(void) {
    return SYSCALL_ABI_VERSION;
}

/* EXIT/EXEC can tear down or replace the caller's address space while this
   dispatcher still owns the interrupt transition. */
__attribute__((no_stack_protector))
u32 syscall_dispatch(u32 nr, u64 a0, u64 a1, u64 a2, u64 a3, u32 caller_task) {
    (void)a3;
    /* One-shot evidence that a ring-3 task actually trapped into the native
       table; without it a silent ring-3 hang is indistinguishable from a
       dispatcher that was never reached. */
    static u8 s_native_trace_done = 0u;
    if (!s_native_trace_done && caller_task != 0u && scheduler_task_is_user(caller_task)) {
        s_native_trace_done = 1u;
        serial_write("[native] table engaged pid ");
        serial_write_dec(caller_task);
        serial_write(" first nr ");
        serial_write_dec(nr);
        serial_writeln("");
    }
    g_syscall_count++;
    ktrace_push(KTRACE_EVT_SYSCALL, 0u, caller_task, nr, a0);

    if (!syscall_policy_allowed(caller_task, nr)) {
        int slot = policy_slot(caller_task, 1);
        if (slot >= 0) g_policy_denied[(u32)slot]++;
        g_syscall_faults++;
        audit_log(AUDIT_EVT_TASK_VIOLATION, caller_task, nr);
        return (u32)-1;
    }

    switch (nr) {
        case KSYSCALL_NOP:
            return 0u;

        case KSYSCALL_WRITE_CHAR:
            kputc((char)(a0 & 0xFFu));
            return 0u;

        case KSYSCALL_WRITE_STR: {
            char tmp[512];
            if (copy_user_str(a0, tmp, sizeof(tmp)) != 0) {
                g_syscall_faults++;
                audit_log(AUDIT_EVT_TASK_VIOLATION, caller_task, a0);
                return (u32)-1;
            }
            kputs(tmp);
            return 0u;
        }

        case KSYSCALL_GET_TICKS:
            return kticks();

        case KSYSCALL_ALLOC: {
            if (a0 == 0u || a0 > (1024u * 1024u)) {
                return 0u;
            }
            if (scheduler_task_is_user(caller_task)) return elf32_user_alloc(caller_task, a0);
            return (u32)(uintptr_t)kalloc((size_t)a0);
        }

        case KSYSCALL_FREE: {
            if (a0 != 0u) {
                if (scheduler_task_is_user(caller_task)) {
                    return elf32_user_free(caller_task, a0) == 0 ? 0u : (u32)-1;
                }
                /* SECURITY: validate pointer falls within kernel heap before
                 * freeing. Prevents arbitrary-address free from kernel tasks
                 * which could cause use-after-free or heap corruption. */
                if (!heap_contains((void*)a0)) {
                    g_syscall_faults++;
                    audit_log(AUDIT_EVT_TASK_VIOLATION, caller_task, a0);
                    return (u32)-1;
                }
                kfree((void*)a0);
            }
            return 0u;
        }

        case KSYSCALL_COMPAT_CALL:
            return (u32)-1;

        case KSYSCALL_GET_TASK_ID:
            return caller_task;

        case KSYSCALL_YIELD:
            /* A user task is still on the syscall interrupt frame here.
               Scheduling it from this dispatcher would switch contexts once,
               then syscall_on_interrupt would switch the same frame again.
               Leave the actual switch to scheduler_schedule_from_irq(). */
            if (!scheduler_task_is_user(caller_task)) schedule();
            return 0u;

        case KSYSCALL_TRACE_EVENT:
            audit_log(AUDIT_EVT_TASK_VIOLATION, a0, caller_task);
            return 0u;

        case KSYSCALL_VERSION:
            return syscall_version();

        case KSYSCALL_OPEN: {
            char path[VFS_PATH_MAX];
            u32 fd = 0u;
            if (syscall_copy_path(a0, path, sizeof(path)) != 0 ||
                kfile_open_for_task(path, a1, &fd, caller_task) != 0) {
                g_syscall_faults++;
                return (u32)-1;
            }
            return fd;
        }

        case KSYSCALL_CLOSE:
            return kfile_close_for_task(a0, caller_task) == 0 ? 0u : (u32)-1;

        case KSYSCALL_READ: {
            /* SECURITY: cap staging buffer to 1024 to prevent kernel stack
             * exhaustion from large VFS_FILE_MAX values. */
            u8 buf[1024];
            u32 len = a2 > sizeof(buf) ? sizeof(buf) : a2;
            int n = kfile_read_for_task(a0, buf, len, caller_task);
            if (n < 0 || (n > 0 && kcopy_to_user(a1, buf, (u32)n) != 0)) return (u32)-1;
            return (u32)n;
        }

        case KSYSCALL_WRITE: {
            u8 buf[1024];
            u32 len = a2 > sizeof(buf) ? sizeof(buf) : a2;
            /* Ring 3 Hardening: Prevent kernel memory information leak via invalid user_src pointer */
            if (len == 0u || kcopy_from_user(buf, a1, len) != 0) return (u32)-1;
            int n = kfile_write_for_task(a0, buf, len, caller_task);
            return n < 0 ? (u32)-1 : (u32)n;
        }

        case KSYSCALL_SEEK: {
            u32 pos = 0u;
            if (kfile_seek_for_task(a0, (i32)a1, a2, &pos, caller_task) != 0) return (u32)-1;
            return pos;
        }

        case KSYSCALL_DUP: {
            u32 fd = 0u;
            if (kfile_dup_for_task(a0, &fd, caller_task) != 0) return (u32)-1;
            return fd;
        }

        case KSYSCALL_STAT: {
            vfs_node_info_t info;
            if (kfile_stat_for_task(a0, &info, caller_task) != 0 ||
                kcopy_to_user(a1, &info, sizeof(info)) != 0) return (u32)-1;
            return 0u;
        }

        case KSYSCALL_EXIT:
            (void)scheduler_exit_task(caller_task, (i32)a0);
            return 0u;

        case KSYSCALL_WAIT: {
            i32 status = 0;
            i32 target_pid = (i32)a0;
            u32 options = (u32)a2;
            u32 reaped_pid = 0;

            if (target_pid == -1 || target_pid == 0) {
                /* Wait for ANY child process */
                int rc = kproc_wait_any(caller_task, &reaped_pid, &status);
                if (rc == 0) {
                    (void)scheduler_reap_task(reaped_pid);
                    if (a1 != 0u) {
                        (void)kcopy_to_user(a1, &status, sizeof(status));
                    }
                    return reaped_pid;
                } else if (rc == -2) {
                    if (options & 1u) { /* WNOHANG */
                        return 0u;
                    }
                    return SYSCALL_WAIT_BLOCKED;
                } else {
                    return (u32)-1;
                }
            } else if (target_pid > 0) {
                u32 pid = (u32)target_pid;
                if (kproc_parent(pid) != caller_task) return (u32)-1;
                int rc = kproc_wait(pid, &status);
                if (rc == 0) {
                    (void)scheduler_reap_task(pid);
                    if (a1 != 0u) {
                        (void)kcopy_to_user(a1, &status, sizeof(status));
                    }
                    return pid;
                } else if (rc == -2) {
                    if (options & 1u) { /* WNOHANG */
                        return 0u;
                    }
                    return SYSCALL_WAIT_BLOCKED;
                } else {
                    return (u32)-1;
                }
            }
            return (u32)-1;
        }

        case KSYSCALL_EXEC: {
            char path[VFS_PATH_MAX];
            u8* image;
            vfs_node_info_t info;
            u32 fd = 0u;
            u32 size = 0u;
            u32 got = 0u;
            elf_exec_info_t exec;
            spinlock_lock(&g_exec_lock);
            image = (u8*)kalloc(USER_ELF_MAX_SIZE);
            if (!image) { spinlock_unlock(&g_exec_lock); return (u32)-1; }
            if (syscall_copy_path(a0, path, sizeof(path)) != 0 ||
                kfile_open(path, KFILE_READ, &fd) != 0 || kfile_stat(fd, &info) != 0) {
                if (fd) (void)kfile_close(fd);
                kfree(image);
                spinlock_unlock(&g_exec_lock);
                return (u32)-1;
            }
            size = info.size;
            if (size == 0u || size > USER_ELF_MAX_SIZE) { (void)kfile_close(fd); kfree(image); spinlock_unlock(&g_exec_lock); return (u32)-1; }
            while (got < size) {
                int n = kfile_read(fd, image + got, size - got);
                if (n <= 0) { (void)kfile_close(fd); kfree(image); spinlock_unlock(&g_exec_lock); return (u32)-1; }
                got += (u32)n;
            }
            (void)kfile_close(fd);
            if (kelf_load_user(image, size, caller_task, &exec) != 0) { kfree(image); spinlock_unlock(&g_exec_lock); return (u32)-1; }
            kfree(image);
            spinlock_unlock(&g_exec_lock);
            return 0u;
        }

        case KSYSCALL_SPAWN: {
            char path[VFS_PATH_MAX];
            u8* image = 0;
            vfs_node_info_t info;
            u32 fd = 0u, size = 0u, got = 0u, pid = 0u;
            elf_exec_info_t exec;
            if (syscall_copy_path(a0, path, sizeof(path)) != 0) {
                return (u32)-1;
            }
            if (kscheduler_create_user_task(path, &pid) != 0) {
                return (u32)-1;
            }
            if (kfile_open(path, KFILE_READ, &fd) != 0 || kfile_stat(fd, &info) != 0 ||
                info.size == 0u || info.size > USER_ELF_MAX_SIZE) {
                if (fd) (void)kfile_close(fd);
                (void)ksched_kill(pid);
                return (u32)-1;
            }
            size = info.size;
            image = (u8*)kalloc(size);
            if (!image) {
                (void)kfile_close(fd);
                (void)ksched_kill(pid);
                return (u32)-1;
            }
            while (got < size) {
                int n = kfile_read(fd, image + got, size - got);
                if (n <= 0) {
                    (void)kfile_close(fd); kfree(image); (void)ksched_kill(pid);
                    return (u32)-1;
                }
                got += (u32)n;
            }
            (void)kfile_close(fd);
            if (kelf_load_user(image, size, pid, &exec) != 0) {
                kfree(image); (void)ksched_kill(pid); return (u32)-1;
            }
            kfree(image);
            return pid;
        }

        case KSYSCALL_THREAD_CREATE: {
            u64 entry = a0;
            u64 stack_top = a1;
            u64 arg = a2;
            (void)arg;
            u64 out_tid_ptr = a3;
            u32 tid = 0u;
            if (scheduler_create_user_thread(caller_task, entry, stack_top, "thread", &tid) != 0) {
                return (u32)-1;
            }
            if (out_tid_ptr) {
                (void)kcopy_to_user(out_tid_ptr, &tid, sizeof(u32));
            }
            return tid;
        }

        case KSYSCALL_SIGNAL: {
            proc_signal_t sig = (proc_signal_t)a1;
            u32 target = a0;
            u32 parent = kproc_parent(target);
            /* A user task may control itself or its direct children only. */
            if (caller_task != 0u && target != caller_task && parent != caller_task) {
                audit_log(AUDIT_EVT_TASK_VIOLATION, caller_task, target);
                return (u32)-1;
            }
            if (sig == PROC_SIG_NONE) {
                return scheduler_task_is_user(target) ? 0u : (u32)-1;
            }
            if (sig != PROC_SIG_TERM && sig != PROC_SIG_KILL &&
                sig != PROC_SIG_USR1 && sig != PROC_SIG_USR2) {
                return (u32)-1;
            }
            if (kproc_signal_send(target, sig) != 0) return (u32)-1;
            /* TERM/KILL are synchronous in this small kernel: terminate the
               target now so a parent waiting immediately cannot starve behind
               a busy user task that has not yielded yet. */
            if (sig == PROC_SIG_TERM || sig == PROC_SIG_KILL) {
                int kill_rc = scheduler_exit_task(
                    target, sig == PROC_SIG_KILL ? 137 : 143);
                /* Destroying another task's image may switch CR3 to the
                   kernel space while releasing its page tables. The caller
                   is still the interrupted user task and must resume in its
                   own address space before the iret path continues. */
                if (scheduler_task_is_user(caller_task)) {
                    (void)paging_space_switch(scheduler_task_address_space(caller_task));
                }
                return kill_rc == 0 ? 0u : (u32)-1;
            }
            return 0u;
        }

        case KSYSCALL_PIPE_CREATE: {
            u32 rfd = 0, wfd = 0;
            if (kpipe_create(&rfd, &wfd, caller_task) != 0) return (u32)-1;
            if (a0 != 0u) {
                u32 fds[2] = { rfd, wfd };
                if (kcopy_to_user(a0, fds, sizeof(fds)) != 0) {
                    (void)kfile_close_for_task(rfd, caller_task);
                    (void)kfile_close_for_task(wfd, caller_task);
                    return (u32)-1;
                }
            }
            return rfd;
        }

        case KSYSCALL_PIPE_READ: {
            int pi = pipe_index(a0);
            u8 chunk[256];
            u32 count, transferred = 0u;
            if (pi < 0 || !pipe_access_allowed(&g_pipes[(u32)pi], caller_task) || !a1 || a2 == 0u) return (u32)-1;
            spinlock_lock(&g_pipe_lock);
            count = a2 < g_pipes[(u32)pi].count ? a2 : g_pipes[(u32)pi].count;
            while (transferred < count) {
                u32 chunk_len = count - transferred;
                if (chunk_len > sizeof(chunk)) chunk_len = sizeof(chunk);
                for (u32 i = 0u; i < chunk_len; i++) {
                    chunk[i] = g_pipes[(u32)pi].data[(g_pipes[(u32)pi].read_pos + transferred + i) % PIPE_CAPACITY];
                }
                if (kcopy_to_user(a1 + transferred, chunk, chunk_len) != 0) {
                    spinlock_unlock(&g_pipe_lock);
                    return transferred > 0 ? transferred : (u32)-1;
                }
                transferred += chunk_len;
            }
            g_pipes[(u32)pi].read_pos = (g_pipes[(u32)pi].read_pos + count) % PIPE_CAPACITY;
            g_pipes[(u32)pi].count -= count;
            spinlock_unlock(&g_pipe_lock);
            return count;
        }

        case KSYSCALL_PIPE_WRITE: {
            int pi = pipe_index(a0);
            u8 chunk[256];
            u32 count, transferred = 0u;
            if (pi < 0 || !pipe_access_allowed(&g_pipes[(u32)pi], caller_task) || !a1 || a2 == 0u) return (u32)-1;
            spinlock_lock(&g_pipe_lock);
            u32 avail = PIPE_CAPACITY - g_pipes[(u32)pi].count;
            count = a2 < avail ? a2 : avail;
            while (transferred < count) {
                u32 chunk_len = count - transferred;
                if (chunk_len > sizeof(chunk)) chunk_len = sizeof(chunk);
                if (kcopy_from_user(chunk, a1 + transferred, chunk_len) != 0) {
                    spinlock_unlock(&g_pipe_lock);
                    return transferred > 0 ? transferred : (u32)-1;
                }
                for (u32 i = 0u; i < chunk_len; i++) {
                    g_pipes[(u32)pi].data[g_pipes[(u32)pi].write_pos] = chunk[i];
                    g_pipes[(u32)pi].write_pos = (g_pipes[(u32)pi].write_pos + 1u) % PIPE_CAPACITY;
                }
                transferred += chunk_len;
            }
            g_pipes[(u32)pi].count += count;
            spinlock_unlock(&g_pipe_lock);
            return count;
        }

        case KSYSCALL_PIPE_CLOSE: {
            int pi = pipe_index(a0);
            if (pi < 0 || !pipe_access_allowed(&g_pipes[(u32)pi], caller_task)) return (u32)-1;
            spinlock_lock(&g_pipe_lock);
            memset(&g_pipes[(u32)pi], 0, sizeof(g_pipes[(u32)pi]));
            spinlock_unlock(&g_pipe_lock);
            return 0u;
        }

        case KSYSCALL_LISTEN: {
            int id = netstack_socket_listen((u16)a0);
            if (id >= 0 && netstack_socket_set_owner((u32)id, caller_task) != 0) {
                (void)netstack_socket_close((u32)id);
                return (u32)-1;
            }
            return id < 0 ? (u32)-1 : (u32)id;
        }

        case KSYSCALL_ACCEPT: {
            u32 client = 0u, remote_ip = 0u;
            u16 remote_port = 0u;
            if (!syscall_socket_allowed(a0, caller_task)) return (u32)-1;
            int result = netstack_socket_accept(a0, &client, &remote_ip, &remote_port);
            if (result != 0 || kcopy_to_user(a1, &client, sizeof(client)) != 0 ||
                (a2 && kcopy_to_user(a2, &remote_ip, sizeof(remote_ip)) != 0) ||
                (a3 && kcopy_to_user(a3, &remote_port, sizeof(remote_port)) != 0)) return (u32)-1;
            if (netstack_socket_set_owner(client, caller_task) != 0) return (u32)-1;
            return 0u;
        }

        case KSYSCALL_SEND: {
            u8 buf[1024];
            u32 len = a2 > sizeof(buf) ? sizeof(buf) : a2;
            if (!syscall_socket_allowed(a0, caller_task)) return (u32)-1;
            if ((len && kcopy_from_user(buf, a1, len) != 0) ||
                netstack_socket_send(a0, buf, (u16)len) != 0) return (u32)-1;
            return len;
        }

        case KSYSCALL_RECV: {
            u8 buf[1024];
            u32 len = a2 > sizeof(buf) ? sizeof(buf) : a2;
            if (!syscall_socket_allowed(a0, caller_task)) return (u32)-1;
            int n = netstack_socket_recv(a0, buf, (u16)len, 0, 0);
            if (n < 0 || (n > 0 && kcopy_to_user(a1, buf, (u32)n) != 0)) return (u32)-1;
            return (u32)n;
        }

        case KSYSCALL_SOCKET_CLOSE:
            if (!syscall_socket_allowed(a0, caller_task)) return (u32)-1;
            return netstack_socket_close(a0) == 0 ? 0u : (u32)-1;

        case KSYSCALL_SOCKET_OPEN: {
            int socket_id = netstack_socket_open(a0, (u16)a1, a2, (u16)a3);
            if (socket_id < 0 || netstack_socket_set_owner((u32)socket_id, caller_task) != 0) {
                if (socket_id >= 0) (void)netstack_socket_close((u32)socket_id);
                return (u32)-1;
            }
            return (u32)socket_id;
        }

        case KSYSCALL_GFX_INFO: {
            u32 info[4];
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0) return (u32)-1;
            info[0] = gfx_width();
            info[1] = gfx_height();
            info[2] = gfx_bpp();
            info[3] = g_gfx_owner_task;
            return kcopy_to_user(a0, info, sizeof(info)) == 0 ? 0u : (u32)-1;
        }

        case KSYSCALL_GFX_MODE: {
            u32 previous;
            int rc;
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0) return (u32)-1;
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            rc = gfx_set_mode((u16)a0, (u16)a1, (u16)a2);
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            return rc == 0 ? 0u : (u32)-1;
        }

        case KSYSCALL_GFX_CLEAR: {
            u32 previous;
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0) return (u32)-1;
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_clear(a0);
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            return 0u;
        }

        case KSYSCALL_GFX_RECT: {
            u32 previous;
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0) return (u32)-1;
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_rect(a0, a1, (a2 >> 16) & 0xFFFFu, a2 & 0xFFFFu, a3);
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            return 0u;
        }

        case KSYSCALL_GFX_ROUND_RECT: {
            u32 previous;
            u32 width = (a2 >> 16) & 0xFFFFu;
            u32 height = a2 & 0xFFFFu;
            u32 radius = (a3 >> 24) & 0xFFu;
            u32 color = a3 & 0x00FFFFFFu;
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0) return (u32)-1;
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_round_rect(a0, a1, width, height, radius, color);
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            return 0u;
        }

        case KSYSCALL_GFX_TEXT: {
            char text[256];
            u32 previous;
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0 ||
                copy_user_str(a3, text, sizeof(text)) != 0) return (u32)-1;
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_text(a0, a1, a2, text);
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            return 0u;
        }

        case KSYSCALL_GFX_EXIT: {
            u32 previous;
            if (!scheduler_task_is_user(caller_task) || g_gfx_owner_task != caller_task) return (u32)-1;
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_exit_graphics();
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            g_gfx_owner_task = 0u;
            return 0u;
        }

        case KSYSCALL_GFX_BLIT: {
            u32 previous;
            u32 width = (a2 >> 16) & 0xFFFFu;
            u32 height = a2 & 0xFFFFu;
            u32 row[256];
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0 ||
                width == 0u || height == 0u || width > 0xFFFFu ||
                a3 < USER_VIRT_BASE ||
                height > 0xFFFFFFFFu / (width * sizeof(u32)) ||
                a3 + (width * height * sizeof(u32)) < a3) return (u32)-1;
            for (u32 row_index = 0u; row_index < height; row_index++) {
                u32 done = 0u;
                while (done < width) {
                    u32 count = width - done;
                    if (count > 256u) count = 256u;
                    if (kcopy_from_user(row, a3 + ((row_index * width + done) * sizeof(u32)),
                                        count * sizeof(u32)) != 0) return (u32)-1;
                    interrupts_disable();
                    previous = gfx_enter_kernel_space();
                    gfx_blit(a0 + done, a1 + row_index, count, 1u, row);
                    gfx_leave_kernel_space(previous);
                    interrupts_enable();
                    done += count;
                }
            }
            /* gfx_blit only writes the kernel backbuffer; without this the
               caller's pixels never reach the surface the display scans out. */
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_present_rect(a0, a1, width, height);
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            /* Every 300 presented blits is a compositor heartbeat on the serial
               console: the first one proves ring-3 pixels reached the scanout,
               later ones prove the frame loop is still running. */
            g_gfx_blits_presented++;
            if (g_gfx_blits_presented == 1u || g_gfx_blits_presented % 300u == 0u) {
                serial_write("[gfx] ring-3 present blit=");
                serial_write_dec(g_gfx_blits_presented);
                serial_write(" rect=");
                serial_write_dec(a0);
                serial_write_dec(a1);
                serial_write(" size=");
                serial_write_dec(width);
                serial_write_dec(height);
                serial_writeln("");
            }
            return 0u;
        }

        case KSYSCALL_INPUT_READ:
            if (!scheduler_task_is_user(caller_task)) return (u32)-1;
            return (u32)keyboard_try_read_char();

        case KSYSCALL_MOUSE_READ: {
            mouse_state_t state;
            i32 xhci_dx = 0, xhci_dy = 0;
            u8  xhci_btns = 0;
            if (!scheduler_task_is_user(caller_task) || !a0) return (u32)-1;
            /* Feed accumulated xHCI HID mouse delta into the PS/2 mouse
               state so USB mice work transparently through the same path. */
            if (xhci_hid_mouse_ready()) {
                xhci_mouse_state(&xhci_dx, &xhci_dy, &xhci_btns);
                if (xhci_dx != 0 || xhci_dy != 0) {
                    mouse_feed_delta(xhci_dx, xhci_dy, xhci_btns);
                }
            }
            mouse_state(&state);
            return kcopy_to_user(a0, &state, sizeof(state)) == 0 ? 0u : (u32)-1;
        }

        case KSYSCALL_SHELL_EXEC: {
            char command[256];
            char output[1024];
            int rc;
            u32 used = 0u;
            if (!scheduler_task_is_user(caller_task) || a1 == 0u || a2 < 2u ||
                copy_user_str(a0, command, sizeof(command)) != 0) return (u32)-1;

            /* SECURITY: require SEC_CAP_SYS_ADMIN for destructive hardware commands */
            if (str_ncmp(command, "format", 6) == 0 ||
                str_ncmp(command, "reboot", 6) == 0 ||
                str_ncmp(command, "halt", 4) == 0 ||
                str_ncmp(command, "heal_test", 9) == 0) {
                if (!security_has_cap(caller_task, SEC_CAP_SYS_ADMIN)) return (u32)-1;
            }

            rc = kconsole_exec_command(command, output, sizeof(output));
            while (output[used] && used + 1u < sizeof(output)) used++;
            if (used + 1u > a2) used = a2 - 1u;
            if (kcopy_to_user(a1, output, used + 1u) != 0) return (u32)-1;
            return (u32)rc;
        }

        case KSYSCALL_MMAP: {
            u64 addr = a0;
            u64 len = a1;
            u32 prot = (u32)a2;
            u32 flags = (u32)a3;
            u64 res = vma_mmap(caller_task, addr, len, prot, flags, -1, 0);
            return (res == (u64)-1) ? (u32)-1 : (u32)res;
        }

        case KSYSCALL_MUNMAP: {
            u64 addr = a0;
            u64 len = a1;
            return vma_munmap(caller_task, addr, len) == 0 ? 0u : (u32)-1;
        }

        case KSYSCALL_MPROTECT: {
            u64 addr = a0;
            u64 len = a1;
            u32 prot = (u32)a2;
            return vma_mprotect(caller_task, addr, len, prot) == 0 ? 0u : (u32)-1;
        }

        case KSYSCALL_CLOCK_GETTIME: {
            u64 user_ts = a1;
            if (!user_ts) return (u32)-1;
            u32 freq = timer_frequency();
            u32 cur_ticks = timer_ticks();
            u64 sec = freq ? (cur_ticks / freq) : 0;
            u64 nsec = 0;
            if (hpet_available()) {
                hpet_status_t hs;
                hpet_status(&hs);
                if (hs.period_fs > 0) {
                    u64 count = hpet_read_counter();
                    nsec = (count * (u64)hs.period_fs) / 1000000ull;
                    nsec %= 1000000000ull;
                } else {
                    nsec = freq ? ((cur_ticks % freq) * (1000000000ull / freq)) : 0;
                }
            } else {
                nsec = freq ? ((cur_ticks % freq) * (1000000000ull / freq)) : 0;
            }
            u64 ts[2] = { sec, nsec };
            return (kcopy_to_user(user_ts, ts, sizeof(ts)) == 0) ? 0u : (u32)-1;
        }

        case KSYSCALL_NANOSLEEP: {
            u64 user_ts = a0;
            if (!user_ts) return (u32)-1;
            u64 req[2] = { 0, 0 };
            if (kcopy_from_user(req, user_ts, sizeof(req)) != 0) return (u32)-1;
            u32 ms = (u32)(req[0] * 1000ull + req[1] / 1000000ull);
            if (ms > 0) {
                timer_delay_ms(ms);
            }
            return 0u;
        }

        case KSYSCALL_DRM_PAGE_FLIP: {
            /* Linux DRM/KMS Atomic Page Flip (Double Buffer Swap)
             * a0: user buffer ptr (const u32* pixels)
             * a1: width
             * a2: height
             */
            u32 width = (u32)a1;
            u32 height = (u32)a2;
            u32 previous;
            u32 row[256];
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0 ||
                width == 0u || height == 0u || width > 3840u || height > 2160u ||
                a0 < USER_VIRT_BASE ||
                height > 0xFFFFFFFFu / (width * sizeof(u32)) ||
                a0 + (width * height * sizeof(u32)) < a0) return (u32)-1;

            for (u32 row_index = 0u; row_index < height; row_index++) {
                u32 done = 0u;
                while (done < width) {
                    u32 count = width - done;
                    if (count > 256u) count = 256u;
                    if (kcopy_from_user(row, a0 + ((row_index * width + done) * sizeof(u32)),
                                        count * sizeof(u32)) != 0) return (u32)-1;
                    interrupts_disable();
                    previous = gfx_enter_kernel_space();
                    gfx_blit(done, row_index, count, 1u, row);
                    gfx_leave_kernel_space(previous);
                    interrupts_enable();
                    done += count;
                }
            }
            /* Atomic Page Flip to hardware linear scanout */
            interrupts_disable();
            previous = gfx_enter_kernel_space();
            gfx_present();
            gfx_leave_kernel_space(previous);
            interrupts_enable();
            return 0u;
        }

        case KSYSCALL_DRM_BLIT: {
            /* DRM Surface Partial Damage Blit
             * a0: dst_x
             * a1: dst_y
             * a2: packed (width << 16 | height)
             * a3: pixels pointer
             */
            u32 previous;
            u32 width = (a2 >> 16) & 0xFFFFu;
            u32 height = a2 & 0xFFFFu;
            u32 row[256];
            if (!scheduler_task_is_user(caller_task) || gfx_claim(caller_task) != 0 ||
                width == 0u || height == 0u || width > 0xFFFFu ||
                a3 < USER_VIRT_BASE ||
                height > 0xFFFFFFFFu / (width * sizeof(u32)) ||
                a3 + (width * height * sizeof(u32)) < a3) return (u32)-1;
            for (u32 row_index = 0u; row_index < height; row_index++) {
                u32 done = 0u;
                while (done < width) {
                    u32 count = width - done;
                    if (count > 256u) count = 256u;
                    if (kcopy_from_user(row, a3 + ((row_index * width + done) * sizeof(u32)),
                                        count * sizeof(u32)) != 0) return (u32)-1;
                    interrupts_disable();
                    previous = gfx_enter_kernel_space();
                    gfx_blit(a0 + done, a1 + row_index, count, 1u, row);
                    gfx_leave_kernel_space(previous);
                    interrupts_enable();
                    done += count;
                }
            }
            return 0u;
        }

        default:
            g_syscall_faults++;
            audit_log(AUDIT_EVT_TASK_VIOLATION, caller_task, nr);
            return (u32)-1;
    }
}

/* Syscalls such as exit/yield replace the active interrupt frame and CR3. */
__attribute__((no_stack_protector))
void syscall_on_interrupt(registers_t* regs) {
    if (!regs) {
        return;
    }
    u32 caller_task = scheduler_current_task();
    int caller_user = scheduler_task_is_user(caller_task);
    if (caller_user) {
        (void)paging_space_switch(scheduler_task_address_space(caller_task));
    }
    u32 nr = (u32)regs->rax;
    u32 ret = nr == KSYSCALL_FORK
        ? syscall_fork_user(regs, caller_task)
        : syscall_dispatch((u32)regs->rax, regs->rbx, regs->rcx, regs->rdx, regs->rsi, caller_task);
    if (ret != SYSCALL_WAIT_BLOCKED) regs->rax = ret;
    if (nr == KSYSCALL_SPAWN && ret != (u32)-1 && caller_task == 0u) {
        scheduler_capture_kernel_context(regs);
    }
    /* A user task must not resume after exit, and yield must restore the
       selected task's complete interrupt frame before iret. */
    if (regs->int_no == 128u &&
        (nr == KSYSCALL_EXIT ||
         (nr == KSYSCALL_YIELD && scheduler_has_runnable_peer(caller_task)))) {
        scheduler_schedule_from_irq(regs);
    } else if (regs->int_no == 128u && nr == KSYSCALL_WAIT && ret == SYSCALL_WAIT_BLOCKED) {
        if (scheduler_block_wait_from_irq(regs, (u32)regs->rbx) != 0) regs->rax = (u32)-1;
        return;
    } else if (regs->int_no == 128u && nr == KSYSCALL_EXEC && ret == 0u) {
        (void)scheduler_current_user_frame(regs);
    }

    if (nr != KSYSCALL_WAIT && nr != KSYSCALL_EXIT && nr != KSYSCALL_YIELD &&
        caller_user) {
        (void)paging_space_switch(scheduler_task_address_space(caller_task));
    }
}


/* Hardware SYSCALL fast-path entrypoint called from interrupt_stubs.asm */
u64 x86_64_syscall_handler(u64 nr, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5, u64 a6) {
    (void)a5;
    (void)a6;
    u32 caller_task = scheduler_current_task();
    int caller_user = scheduler_task_is_user(caller_task);
    if (caller_user) {
        (void)paging_space_switch(scheduler_task_address_space(caller_task));
    }
    u32 ret = syscall_dispatch((u32)nr, a1, a2, a3, a4, caller_task);
    if (caller_user && nr != KSYSCALL_EXIT) {
        (void)paging_space_switch(scheduler_task_address_space(caller_task));
    }
    return (u64)ret;
}
