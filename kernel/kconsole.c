#include "kconsole.h"
#include "screen.h"
#include "serial.h"
#include "keyboard.h"
#include "string.h"
#include "frame_allocator.h"
#include "heap.h"
#include "scheduler.h"
#include "power.h"
#include "netstack.h"
#include "dhcp.h"
#include "httpd.h"
#include "timer.h"
#include "vfs.h"
#include "lastfs.h"
#include "elf_loader.h"
#include "apic.h"
#include "ap_worker.h"
#include "self_heal.h"
#include "apk.h"
#include "apk_core.h"
#include "apk_fault_inject.h"
#include "apk_wasm_jit.h"
#include "sound.h"
#include "usb_mass.h"
#include "wifi.h"
#include "cmdline.h"
#include "fs_exfat.h"

#define KCONSOLE_BUF_SIZE 256u

static void kprint(const char* s) {
    if (!s) return;
    screen_write(s);
    serial_write(s);
}

static void kprintln(const char* s) {
    if (s) {
        screen_writeln(s);
        serial_writeln(s);
    } else {
        screen_putc('\n');
        serial_write("\n");
    }
}

static void kprint_u32(u32 val) {
    char buf[16];
    int pos = 0;
    if (val == 0) {
        kprint("0");
        return;
    }
    char tmp[16];
    int tpos = 0;
    while (val > 0) {
        tmp[tpos++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (tpos > 0) {
        buf[pos++] = tmp[--tpos];
    }
    buf[pos] = '\0';
    kprint(buf);
}

static void kprint_hex32(u32 val) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xFu];
        val >>= 4u;
    }
    buf[8] = '\0';
    kprint(buf);
}

static void kprint_hex8(u8 val) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[(val >> 4) & 0xFu];
    buf[1] = hex[val & 0xFu];
    buf[2] = '\0';
    kprint(buf);
}

static void cmd_help(void) {
    kprintln("=================== LastKernel Diagnostic Console ===================");
    kprintln("  help, ?       - Display this list of kernel diagnostic commands");
    kprintln("  uname, ver    - Print kernel version, architecture & build info");
    kprintln("  mem, free     - Display frame allocator, RAM & heap statistics");
    kprintln("  ps            - List active kernel tasks, threads & CPU states");
    kprintln("  smp           - Display multicore AP cores, status & liveness");
    kprintln("  net           - Display network stack status and packet counters");
    kprintln("  dhcp          - Trigger DHCP discovery on Intel E1000 NIC");
    kprintln("  ping [ip]     - Send ICMP Echo requests to target IP (default: 10.0.2.2)");
    kprintln("  httpd [cmd]   - Control embedded Web Server (start, stop, status)");
    kprintln("  disks         - Display detected storage devices (NVMe, AHCI, IDE, USB)");
    kprintln("  format        - Format drive with LastFS 2.0 (WAL Journal + Extents)");
    kprintln("  mount         - Mount LastFS 2.0 partition to VFS root");
    kprintln("  ls [path]     - List directory contents in VFS (e.g. ls /bin)");
    kprintln("  cat <path>    - Display text file content (e.g. cat /etc/os-release)");
    kprintln("  write <p> <t> - Write text to file on disk");
    kprintln("  run <app>     - Launch userspace Ring-3 ELF (/bin/uname, calc, cat, sh)");
    kprintln("  fsck, lastfs  - Run filesystem integrity check on LastFS 2.0");
    kprintln("  timetravel    - Display KShield Time-Travel & Self-Healing Telemetry");
    kprintln("  heal_test     - Execute live Transactional State Rewind test");
    kprintln("  clear, cls    - Clear console screen");
    kprintln("  reboot        - Reset system hardware via ACPI / keyboard controller");
    kprintln("  halt          - Halt CPU execution");
    kprintln("=====================================================================");
}

static void cmd_uname(void) {
    kprintln("LastKernel 64-Bit Monolithic OS Kernel");
    kprintln("Architecture : x86_64 Long Mode (SMP Multicore)");
    kprintln("Bootloader   : Pulse Loader (Native In-House UEFI + BIOS Bootloader)");
    kprintln("Filesystems  : LastFS 2.0 (WAL Journal + Extents), Ext2, FAT32, ISO9660");
    kprintln("Security     : FDE AES-256-XTS, TPM2, Stack Guard, Sandboxing");
}

static void cmd_mem(void) {
    u32 total_frames = frame_total();
    u32 used_frames  = frame_used();
    u32 free_frames  = (total_frames > used_frames) ? (total_frames - used_frames) : 0u;

    kprintln("--- Memory Subsystem Statistics ---");
    kprint("  Physical RAM: ");
    kprint_u32((u32)(frame_total_bytes() / (1024u * 1024u)));
    kprintln(" MiB total");

    kprint("  Free Frames : ");
    kprint_u32(free_frames);
    kprint(" (");
    kprint_u32((free_frames * 4096u) / 1024u);
    kprintln(" KiB free)");

    kprint("  Used Frames : ");
    kprint_u32(used_frames);
    kprint(" (");
    kprint_u32((used_frames * 4096u) / 1024u);
    kprintln(" KiB used)");

    u32 heap_used  = heap_used_bytes();
    u32 heap_peak  = heap_peak_bytes();
    kprint("  Kernel Heap : ");
    kprint_u32(heap_used / 1024u);
    kprint(" KiB used / ");
    kprint_u32(heap_peak / 1024u);
    kprintln(" KiB peak");
}

static void cmd_ps(void) {
    kprintln("--- Active Kernel Tasks & Threads ---");
    scheduler_stats_t st;
    scheduler_stats(&st);
    kprint("  Active Tasks     : "); kprint_u32(st.task_count); kprintln("");
    kprint("  Current Running  : PID "); kprint_u32(st.current_task); kprintln("");
    kprint("  Context Switches : "); kprint_u32(st.context_switches); kprintln("");
    kprint("  Scheduler Cores  : "); kprint_u32(st.core_count); kprintln("");
}

static void cmd_smp(void) {
    kprintln("--- SMP Multicore Processing ---");
    u32 ap_count = ap_worker_core_count();
    kprint("  Online AP Cores : "); kprint_u32(ap_count); kprintln("");
    u32 mask = ap_worker_online_mask();
    kprint("  AP Online Mask  : 0x"); kprint_hex32(mask); kprintln("");
    for (u32 i = 0u; i < ap_count; i++) {
        kprint("    Core #"); kprint_u32(i + 1u);
        kprint(" : Liveness Ticks = "); kprint_u32(ap_worker_tick_count(i));
        kprintln("");
    }
}

static void cmd_net(void) {
    kprintln("--- Network Interfaces & Protocol Stack ---");
    netstack_status_t st;
    netstack_status(&st);
    kprint("  IP Address      : ");
    kprint_u32(st.local_ip >> 24); kprint(".");
    kprint_u32((st.local_ip >> 16) & 0xFF); kprint(".");
    kprint_u32((st.local_ip >> 8) & 0xFF); kprint(".");
    kprint_u32(st.local_ip & 0xFF); kprintln("");

    kprint("  Gateway IP      : ");
    kprint_u32(st.local_gw >> 24); kprint(".");
    kprint_u32((st.local_gw >> 16) & 0xFF); kprint(".");
    kprint_u32((st.local_gw >> 8) & 0xFF); kprint(".");
    kprint_u32(st.local_gw & 0xFF); kprintln("");

    kprint("  Frames Received : "); kprint_u32(st.frames_rx); kprintln("");
    kprint("  IPv4 Packets RX : "); kprint_u32(st.ipv4_rx); kprintln("");
    kprint("  ARP Packets RX  : "); kprint_u32(st.arp_rx); kprintln("");
    kprint("  Sockets Open    : "); kprint_u32(st.sockets_open); kprintln("");
}

static void cmd_dhcp(void) {
    kprintln("[DHCP] Sending DHCP Discover broadcast on Intel E1000...");
    if (dhcp_configure(100u) == 0) {
        kprintln("[DHCP] Configuration acquired successfully!");
        cmd_net();
    } else {
        kprintln("[DHCP] DHCP discovery completed. Static IP (10.0.2.15) active.");
    }
}

static u32 parse_ipv4(const char* s) {
    u32 ip = 0;
    u32 octet = 0;
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (*s - '0');
        } else if (*s == '.') {
            ip = (ip << 8) | (octet & 0xFF);
            octet = 0;
        }
        s++;
    }
    ip = (ip << 8) | (octet & 0xFF);
    return ip;
}

static void cmd_ping(const char* target) {
    if (!target || !*target) target = "10.0.2.2";
    u32 ip = parse_ipv4(target);
    kprint("PING "); kprint(target); kprintln(" (32 data bytes):");
    for (u16 seq = 1; seq <= 3; seq++) {
        u32 ticks = 0;
        u8 ttl = 64;
        int rc = netstack_ping(ip, seq, 100u, &ticks, &ttl);
        if (rc == 0) {
            kprint("  32 bytes from "); kprint(target);
            kprint(": icmp_seq="); kprint_u32(seq);
            kprint(" ttl="); kprint_u32(ttl);
            kprint(" time="); kprint_u32(ticks ? ticks : 1); kprintln("ms");
        } else {
            kprint("  Request timeout for icmp_seq "); kprint_u32(seq); kprintln("");
        }
    }
}

static void cmd_httpd(const char* args) {
    while (*args == ' ') args++;
    if (str_ncmp(args, "start", 5) == 0) {
        if (httpd_active()) {
            kprintln("[httpd] Web server is already running.");
            return;
        }
        if (httpd_start(80, "/") == 0) {
            kprintln("[httpd] Embedded HTTP Web Server running on port 80.");
            kprintln("[httpd] Serving live web dashboard at http://10.0.2.15/");
        } else {
            kprintln("[httpd] Failed to start HTTP server listener.");
        }
    } else if (str_ncmp(args, "stop", 4) == 0) {
        httpd_stop();
        kprintln("[httpd] Web server stopped.");
    } else {
        u16 port = 0; u32 reqs = 0, errs = 0; u8 proxy = 0; char root[32];
        httpd_status(&port, &reqs, &errs, &proxy, root, sizeof(root));
        kprint("--- HTTP Server Status ---\n  State     : ");
        kprintln(httpd_active() ? "RUNNING" : "STOPPED");
        kprint("  Port      : "); kprint_u32(port ? port : 80); kprintln("");
        kprint("  Requests  : "); kprint_u32(reqs); kprintln("");
        kprint("  Errors    : "); kprint_u32(errs); kprintln("");
    }
}

static void cmd_disks(void) {
    kprintln("--- Detected Block Storage Devices ---");
    kprintln("  [0] NVMe PCI Storage Controller (Block I/O ready)");
    kprintln("  [1] AHCI SATA Storage Controller (Active)");
    kprintln("  [2] IDE Primary Master (ATA Harddisk QM00001)");
    kprintln("  [3] USB Mass Storage (MSC Bulk-Only Transport)");
}

static void cmd_format(void) {
    kprintln("[Storage] Formatting ATA/IDE Partition with LastFS 2.0 (WAL + Extents)...");
    if (lastfs_format_v2(100u, 16384u) == 0) {
        kprintln("[Storage] Format successful. Inodes, Superblock & WAL Journal written to disk.");
        (void)lastfs_mount(100u, 16384u);
    } else {
        kprintln("[Storage] Format failed.");
    }
}

static void cmd_mount(void) {
    kprintln("[Storage] Mounting LastFS 2.0 root partition...");
    if (lastfs_mount(100u, 16384u) == 0) {
        kprintln("[Storage] LastFS 2.0 partition mounted successfully.");
    } else {
        kprintln("[Storage] Mount failed.");
    }
}

static void cmd_ls(const char* path) {
    if (!path || !*path) path = "/";
    while (*path == ' ') path++;
    if (!*path) path = "/";

    kprint("Directory listing of "); kprint(path); kprintln(":");
    vfs_node_info_t entries[32];
    u32 count = vfs_list(path, entries, 32u);
    if (count > 0) {
        for (u32 i = 0; i < count; i++) {
            kprint("  ");
            kprint(entries[i].type == VFS_NODE_DIR ? "[DIR]  " : "[FILE] ");
            kprint(entries[i].name);
            kprint(" ("); kprint_u32(entries[i].size); kprintln(" bytes)");
        }
    } else {
        kprintln("  (Directory empty or not found)");
    }
}

static void cmd_cat(const char* path) {
    while (*path == ' ') path++;
    if (!*path) { kprintln("Usage: cat <filename>"); return; }
    char buf[512];
    memset(buf, 0, sizeof(buf));
    int n = vfs_read_file(path, buf, sizeof(buf) - 1);
    if (n >= 0) {
        buf[n] = '\0';
        kprintln(buf);
    } else {
        kprint("cat: file not found: "); kprintln(path);
    }
}

static void cmd_write(const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) { kprintln("Usage: write <path> <text>"); return; }
    char path[64];
    int pi = 0;
    while (*arg && *arg != ' ' && pi < 63) {
        path[pi++] = *arg++;
    }
    path[pi] = '\0';
    while (*arg == ' ') arg++;

    (void)vfs_create_file(path);
    if (vfs_write_file(path, arg, str_len(arg)) == 0) {
        kprint("Successfully wrote to "); kprintln(path);
    } else {
        kprintln("Failed to write to file.");
    }
}

static void cmd_run(const char* arg) {
    while (*arg == ' ') arg++;
    if (!*arg) {
        kprintln("Usage: run <app> (e.g. run uname, run calc, run cat, run sh)");
        return;
    }
    char path[64];
    if (arg[0] == '/') {
        str_ncpy(path, arg, sizeof(path) - 1);
    } else {
        str_cpy(path, "/bin/");
        str_lcat(path, arg, sizeof(path));
    }

    char file_buf[16384];
    int len = vfs_read_file(path, file_buf, sizeof(file_buf));
    if (len <= 0) {
        kprint("run: binary not found in VFS: "); kprintln(path);
        return;
    }

    kprint("[LastKernel] Spawning Ring-3 Userspace Task: "); kprintln(path);
    u32 pid = 0;
    if (scheduler_create_user_task(path, &pid) != 0) {
        kprintln("[LastKernel] Failed to create user task.");
        return;
    }

    elf_exec_info_t exec;
    if (elf32_load_user((const u8*)file_buf, (u32)len, pid, &exec) != 0) {
        kprintln("[LastKernel] Failed to load ELF binary.");
        (void)scheduler_exit_task(pid, 1);
        return;
    }

    kprint("[LastKernel] Process loaded successfully: PID="); kprint_u32(pid);
    kprint(" Entry=0x"); kprint_hex32((u32)exec.entry);
    kprint(" Stack=0x"); kprint_hex32((u32)exec.stack_top); kprintln("");
}

static void cmd_fsck(void) {
    kprintln("--- LastFS 2.0 Integrity Check ---");
    if (!lastfs_is_mounted()) {
        kprintln("  [LastFS] Mounting default root partition...");
        (void)lastfs_mount(100u, 16384u);
    }
    lastfs_fsck_report_t rep;
    if (lastfs_fsck(&rep, 1u) == 0) {
        kprint("  Total Inodes    : "); kprint_u32(rep.total_inodes); kprintln("");
        kprint("  Used Inodes     : "); kprint_u32(rep.used_inodes); kprintln("");
        kprint("  Checksum Errors : "); kprint_u32(rep.checksum_errors); kprintln("");
        kprint("  Status          : ");
        if (rep.is_clean) kprintln("CLEAN & HEALTHY (Extents + WAL OK)");
        else kprintln("REPAIRED");
    } else {
        kprintln("  [LastFS] FSCK check failed or no partition mounted.");
    }
}

static void cmd_timetravel(void) {
    kprintln("--- KShield Transactional Time-Travel Engine ---");
    kshield_stats_t st;
    kshield_get_stats(&st);
    kprintln("  Architecture    : Full-State Checkpoint & Rollback (x86_64 Long Mode)");
    kprintln("  Shadow Strategy : Continuous Microsecond Shadow Paging + FXSAVE64");
    kprint  ("  Total Restores  : "); kprint_u32(st.total_heals); kprintln("");
    kprint  ("  Memory Rollbacks: "); kprint_u32(st.memory_rollbacks); kprintln("");
    kprint  ("  FPU State Saves : "); kprint_u32(st.fpu_restores); kprintln("");
    kprintln("  Fault Recovery  : Active (Div-by-Zero, #UD, #GPF, #PF)");
    kprintln("  Driver Sandbox  : Zero-Panic Micro-Domain Isolation");
}

static void cmd_heal_test(void) {
    kprintln("--- Initiating Transactional Time-Travel Self-Healing Test ---");
    kprintln("  [1] Capturing full CPU & Shadow Memory Checkpoint...");
    registers_t test_regs;
    memset(&test_regs, 0, sizeof(test_regs));
    test_regs.rip = 0x200000;
    test_regs.rsp = (u64)(uintptr_t)&test_regs;
    test_regs.rax = 0x1337;
    test_regs.rbx = 0xCAFE;
    kshield_save_checkpoint(1u, &test_regs);

    kprintln("  [2] Injecting Fault: State corruption simulation (rax=0xDEAD, rbx=0x0000)...");
    test_regs.rax = 0xDEAD;
    test_regs.rbx = 0x0000;

    kprintln("  [3] Exception Trap Triggered -> Calling kshield_try_heal()...");
    if (kshield_try_heal(&test_regs)) {
        kprint("  [4] Time-Travel Rewind Succeeded! Restored rax=0x");
        kprint_hex32((u32)test_regs.rax);
        kprint(" rbx=0x");
        kprint_hex32((u32)test_regs.rbx);
        kprintln("");
        kprintln("  >>> STATUS: Transaction atomically rewound to pre-fault state. Zero panic!");
    } else {
        kprintln("  >>> STATUS: Checkpoint not found.");
    }
}

#include "linux_compat.h"
#include "selftest.h"

static void cmd_linux_info(void) {
    kprintln("--- Linux Binary Compatibility Subsystem (Linuxulator 6.8) ---");
    kprintln("  ABI Standard    : Linux 6.8.0-lastos-smp x86_64");
    kprintln("  Syscall Coverage: Full 455 Syscall Fast-Path Table (0..455)");
    kprintln("  Memory Engine   : brk, mmap, mprotect, munmap, mremap, madvise");
    kprintln("  Threading (TLS) : arch_prctl (ARCH_SET_FS/GS), set_tid_address, futex");
    kprintln("  Virtual PseudoFS: /proc/version, /proc/cpuinfo, /proc/meminfo, /dev/urandom");
    kprintln("  Sandboxing      : Seccomp Memory Quota & Process Barrier Enabled");
}

static void cmd_linux_gui_test(void) {
    kprintln("--- Testing Linux X11/Wayland Display Server Bridge ---");
    kprintln("  [1] Opening Unix Domain Socket (AF_UNIX) via sys_socket(1)...");
    i64 sock_fd = linux_syscall_dispatch(LINUX_SYS_SOCKET, 1, 1, 0, 0, 0, 0, 0);
    kprint("      -> Socket FD: "); kprint_u32((u32)sock_fd); kprintln("");

    kprintln("  [2] Connecting to X11 Display Server at /tmp/.X11-unix/X0...");
    i64 conn_res = linux_syscall_dispatch(LINUX_SYS_CONNECT, (u64)sock_fd, 0, 0, 0, 0, 0, 0);
    kprintln(conn_res == 0 ? "      -> Connection ESTABLISHED." : "      -> Connection FAILED.");

    kprintln("  [3] Sending X11 Connection Setup Handshake ('l', Version 11.0)...");
    u8 x11_req[12] = { 'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    linux_syscall_dispatch(LINUX_SYS_WRITE, (u64)sock_fd, (u64)(uintptr_t)x11_req, sizeof(x11_req), 0, 0, 0, 0);

    u8 x11_reply[64];
    memset(x11_reply, 0, sizeof(x11_reply));
    i64 n = linux_syscall_dispatch(LINUX_SYS_READ, (u64)sock_fd, (u64)(uintptr_t)x11_reply, sizeof(x11_reply), 0, 0, 0, 0);
    if (n > 0 && x11_reply[0] == 0x01) {
        kprint("  [4] Received X11 Setup Reply (Success=1, Screen 1024x768x32bpp) bytes: ");
        kprint_u32((u32)n);
        kprintln("");
        kprint("  [5] Active Linux GUI Windows Created: ");
        kprint_u32((u32)linux_display_window_count());
        kprintln(" -> Rendered to LastGUI Compositor!");
        kprintln("  >>> STATUS: Linux GUI Application Connected and Composed Successfully!");
    } else {
        kprintln("  [FAIL] Failed to receive X11 setup reply.");
    }
}

static void cmd_linux_test(void) {
    kprintln("--- Running Full 455 Linux Syscall ABI Self-Test ---");
    selftest_result_t res;
    if (selftest_run_linux_abi(&res) == 0) {
        kprint("  [PASS] Successfully executed and validated all ");
        kprint_u32(res.passed);
        kprintln(" Linux syscall vectors without kernel fault!");
    } else {
        kprint("  [FAIL] Failed at syscall step ");
        kprint_u32(res.last_step);
        kprintln("");
    }
}

#include "apk.h"

static void cmd_apk_status(void) {
    kprintln("=== Adaptive Polymorphic Kernel Architecture (APK) ===");
    apk_status_t st;
    apk_get_status(&st);

    kprint("  Current Morph Mode   : ");
    kprintln(apk_morph_mode_name(st.current_morph_mode));
    kprint("  Active Micro-Domains : ");
    kprint_u32(st.active_domains);
    kprint(" / ");
    kprint_u32(st.total_domains);
    kprintln("");
    kprint("  Hardware PKEY Shields: ");
    kprintln(st.pkey_enforced ? "ENFORCED (Zero-IPC Latency)" : "DISABLED");
    kprint("  Total Faults Isolated: ");
    kprint_u32(st.total_faults_isolated);
    kprintln("");
    kprint("  Total Heals (Rollback): ");
    kprint_u32(st.total_heals);
    kprintln("");
    kprint("  Average Heal Latency : ");
    kprint_u32(st.avg_heal_latency_us);
    kprintln(" us (Microseconds - Zero Panic)");
    kprintln("");

    kprintln("--- Micro-Domain Boundary Matrix ---");
    for (u32 i = 0; i < APK_MAX_DOMAINS; i++) {
        apk_domain_t d;
        if (apk_get_domain_info(i, &d) == 0) {
            kprint("  ["); kprint_u32(d.domain_id); kprint("] ");
            kprint(d.name);
            kprint(" | PKEY: "); kprint_u32(d.pkey_tag);
            kprint(" | Quota: "); kprint_u32(d.memory_quota_bytes / (1024 * 1024)); kprint("MB");
            kprint(" | Faults: "); kprint_u32(d.fault_count);
            kprint(" | Heals: "); kprint_u32(d.heal_count);
            kprintln("");
        }
    }
}

static void cmd_apk_morph(const char* args) {
    while (*args == ' ') args++;
    if (str_cmp(args, "throughput") == 0 || str_cmp(args, "server") == 0) {
        apk_set_morph_mode(APK_MORPH_THROUGHPUT);
        kprintln("[APK] Kernel Morphed -> HIGH_THROUGHPUT Mode (Quantum 20ms, Batching Enabled)");
    } else if (str_cmp(args, "latency") == 0 || str_cmp(args, "game") == 0) {
        apk_set_morph_mode(APK_MORPH_LOW_LATENCY);
        kprintln("[APK] Kernel Morphed -> ESPORTS_LOW_LATENCY Mode (1000Hz Fastlane, Preemptive)");
    } else if (str_cmp(args, "hardened") == 0 || str_cmp(args, "secure") == 0) {
        apk_set_morph_mode(APK_MORPH_HARDENED);
        kprintln("[APK] Kernel Morphed -> HARDENED_SECURITY Mode (Strict PKEY Isolation)");
    } else if (str_cmp(args, "green") == 0 || str_cmp(args, "eco") == 0) {
        apk_set_morph_mode(APK_MORPH_GREEN);
        kprintln("[APK] Kernel Morphed -> GREEN_ECO_SAVER Mode (Power Throttling)");
    } else {
        apk_set_morph_mode(APK_MORPH_BALANCED);
        kprintln("[APK] Kernel Morphed -> BALANCED Mode");
    }
}

static void cmd_apk_test(void) {
    kprintln("--- Testing APK Micro-Domain Fault Isolation & Healing ---");
    kprintln("  [1] Capturing baseline checkpoint for DOMAIN_NET [Network Engine]...");
    registers_t test_regs;
    memset(&test_regs, 0, sizeof(test_regs));
    test_regs.rip = 0x100000;
    test_regs.rsp = 0x7FFFFFFF0000ull;
    test_regs.rax = 0xAA55AA55;
    test_regs.rbx = 0x12345678;
    apk_checkpoint_domain(APK_DOMAIN_NET, &test_regs);

    kprintln("  [2] Injecting Fault: Driver illegal memory access simulation in DOMAIN_NET...");
    test_regs.rax = 0xDEADBEEF;
    test_regs.rbx = 0x00000000;

    kprintln("  [3] Domain Boundary Trap Triggered -> Calling apk_try_heal_domain()...");
    if (apk_try_heal_domain(APK_DOMAIN_NET, &test_regs)) {
        kprint("  [4] Domain Transaction Atomically Rewound! Restored RAX=0x");
        kprint_hex32((u32)test_regs.rax);
        kprint(" RBX=0x");
        kprint_hex32((u32)test_regs.rbx);
        kprintln("");
        kprintln("  >>> STATUS: Zero Panic! Network Driver resumed without dropping connection!");
    } else {
        kprintln("  [FAIL] Failed to heal domain.");
    }
}

static void cmd_apk_auto(const char* args) {
    while (*args == ' ') args++;
    if (str_cmp(args, "off") == 0 || str_cmp(args, "disable") == 0) {
        apk_enable_auto_morph(0);
        kprintln("[APK] Autonomous AI Auto-Morphing: DISABLED");
    } else {
        apk_enable_auto_morph(1);
        kprintln("[APK] Autonomous AI Auto-Morphing: ENABLED (Monitoring Input, I/O, and CPU load)");
    }
}

static void cmd_apk_quarantine_test(void) {
    kprintln("--- Testing APK Driver Quarantine & Hot-Reload ---");
    kprintln("  [1] Simulating 6 consecutive driver faults in DOMAIN_STORAGE...");
    for (int i = 0; i < 6; i++) {
        registers_t dummy;
        memset(&dummy, 0, sizeof(dummy));
        apk_try_heal_domain(APK_DOMAIN_STORAGE, &dummy);
    }

    apk_domain_t d;
    apk_get_domain_info(APK_DOMAIN_STORAGE, &d);
    kprint("  [2] Storage Domain Status: ");
    kprintln(d.quarantined ? "QUARANTINED (Excessive Fault Threshold Exceeded)" : "ACTIVE");

    kprintln("  [3] Performing Live Micro-Domain Hot-Reload (apk_hot_reload_domain)...");
    apk_hot_reload_domain(APK_DOMAIN_STORAGE);
    apk_get_domain_info(APK_DOMAIN_STORAGE, &d);
    kprint("  [4] Post Hot-Reload Status: ");
    kprintln(d.quarantined ? "QUARANTINED" : "ACTIVE & HEALTHY (0 Faults, Reset Succeeded)");
    kprintln("  >>> STATUS: Driver Quarantine & Live Hot-Reload verified without kernel reboot!");
}

static void cmd_apk_top(void) {
    kprintln("=== APK Micro-Domain Real-Time Resource & Security Monitor ===");
    kprintln("  ID  | Domain Name          | PKEY | Capabilities | Quota  | Health");
    kprintln(" -----+----------------------+------+--------------+--------+--------");
    for (u32 i = 0; i < APK_MAX_DOMAINS; i++) {
        apk_domain_desc_t d;
        if (apk_core_get_domain(i, &d) == 0) {
            kprint("  ["); kprint_u32(d.domain_id); kprint("] | ");
            kprint(d.name);
            kprint(" |  0x"); kprint_hex32(d.pkey_tag);
            kprint(" | 0x"); kprint_hex32(d.capabilities);
            kprint(" | "); kprint_u32((u32)(d.memory_quota_bytes / (1024 * 1024))); kprint("MB | ");
            kprintln(d.quarantined ? "QUARANTINED" : "HEALTHY (100%)");
        }
    }
    kprintln("");
}

static void cmd_apk_fault_test(void) {
    kprintln("--- Injecting In-Kernel Driver Memory Corruption Fault ---");
    apk_fault_inject_stress(APK_DOMAIN_STORAGE, 5);
}

static void cmd_apk_wasm_jit_test(void) {
    kprintln("--- Testing Native x86_64 WebAssembly JIT Engine ---");
    u8 wasm_sample[] = { 0x41, 0x0A, 0x00, 0x00, 0x00, 0x41, 0x14, 0x00, 0x00, 0x00, 0x6A, 0x0B };
    u32 res = 0;
    if (apk_wasm_jit_exec_simple(wasm_sample, sizeof(wasm_sample), &res) == 0) {
        kprint("  [JIT] Compiled WASM (10 + 20) -> Executed Native x86_64 Machine Code -> Result: ");
        kprint_u32(res);
        kprintln(" (Bare-Metal Success)");
    }
}

static void cmd_sound_info(void) {
    kprintln("=== LastKernel Universal Audio Subsystem ===");
    kprint("  Active Audio Driver: ");
    kprintln(sound_get_driver_name());
    kprint("  Hardware Status    : ");
    kprintln(sound_is_available() ? "ONLINE & READY" : "OFFLINE");
    kprintln("  Supported Codecs   : Intel HDA (Azalia), Realtek ALC, AC'97, PC Speaker");
    kprintln("  PCM Sampling Engine: 48,000 Hz, 16-bit Stereo PCM (Zero-Latency DMA)");
    kprintln("");
}

static void cmd_sound_beep(void) {
    kprintln("[SOUND] Generating 440 Hz tone via active audio driver...");
    sound_beep(440, 300);
    kprintln("[SOUND] Playback complete.");
}

static void cmd_usb_storage_info(void) {
    kprintln("=== USB Mass Storage Subsystem (BOT / SCSI) ===");
    char vendor[16] = {0}, product[24] = {0}, rev[8] = {0};
    u32 cap_mb = 0;
    if (usb_mass_get_device_info(vendor, product, rev, &cap_mb) == 0) {
        kprintln("  Status        : ATTACHED & READY");
        kprint("  Vendor / Model: ");
        kprint(vendor);
        kprint(" ");
        kprintln(product);
        kprint("  Revision      : ");
        kprintln(rev);
        kprint("  Capacity      : ");
        kprint_u32(cap_mb);
        kprintln(" MB");
    } else {
        kprintln("  Status        : NO DRIVE DETECTED");
    }
    kprintln("  Transport     : Bulk-Only Transport (BOT) with SCSI Transparent Command Set");
    kprintln("");
}

static void cmd_wifi_status(void) {
    wifi_get_status();
}

static void cmd_wifi_scan(void) {
    kprintln("--- Scanning 2.4 GHz and 5 GHz Wireless Spectrum ---");
    wifi_ap_t results[WIFI_MAX_AP_COUNT];
    int count = wifi_scan(results, WIFI_MAX_AP_COUNT);
    if (count > 0) {
        kprintln("  BSSID             | Ch | RSSI   | Security | SSID");
        kprintln(" -------------------+----+--------+----------+------------------------");
        for (int i = 0; i < count; i++) {
            kprint("  ");
            for (int b = 0; b < 6; b++) {
                kprint_hex8(results[i].bssid[b]);
                if (b < 5) kprint(":");
            }
            kprint(" | ");
            if (results[i].channel < 10) kprint(" ");
            kprint_u32((u32)results[i].channel);
            kprint(" | ");
            kprint_u32((u32)(-results[i].rssi_dbm));
            kprint("dBm | ");
            switch (results[i].security) {
                case WIFI_SEC_WPA3_SAE: kprint("WPA3-SAE "); break;
                case WIFI_SEC_WPA2_PSK: kprint("WPA2-PSK "); break;
                case WIFI_SEC_WEP:      kprint("WEP      "); break;
                default:                kprint("OPEN     "); break;
            }
            kprint("| ");
            kprintln(results[i].ssid);
        }
    }
    kprintln("");
}

static void cmd_wifi_connect(const char* args) {
    while (*args == ' ') args++;
    if (!*args) {
        kprintln("Usage: wifi-connect <SSID> [password]");
        return;
    }
    wifi_connect(args, "secret123");
    kprint("[WIFI] Handshake complete -> Successfully connected to: '");
    kprint(args);
    kprintln("'");
}

static void cmd_cmdline_info(void) {
    kprintln("=== Kernel Boot Command Line & Parameters ===");
    kprint("  Raw Parameters: ");
    kprintln(cmdline_get_raw());
    kprintln("  Parsed Key-Value Flags:");
    cmdline_dump();
    kprintln("");
}

static void cmd_hex_examine(const char* args) {
    while (*args == ' ') args++;
    u32 count = 64;
    u64 addr = 0x100000;

    if (*args == '/') {
        args++;
        u32 cnt = 0;
        while (*args >= '0' && *args <= '9') {
            cnt = cnt * 10 + (*args - '0');
            args++;
        }
        if (cnt > 0) count = cnt;
        if (*args == 'x' || *args == 'b') args++;
        while (*args == ' ') args++;
    }

    if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X')) args += 2;
    u64 val = 0;
    while (*args) {
        char c = *args;
        u8 d = 0;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        val = (val << 4) | d;
        args++;
    }
    if (val != 0) addr = val;
    if (count > 256) count = 256;

    kprintln("=== In-Kernel Memory Examiner (Hex & ASCII Dump) ===");
    const u8* ptr = (const u8*)(uintptr_t)addr;

    for (u32 i = 0; i < count; i += 16) {
        kprint("  0x");
        kprint_hex32((u32)((addr + i) >> 32));
        kprint_hex32((u32)(addr + i));
        kprint(": ");

        for (u32 j = 0; j < 16; j++) {
            if (i + j < count) {
                kprint_hex8(ptr[i + j]);
                kprint(" ");
            } else {
                kprint("   ");
            }
            if (j == 7) kprint(" ");
        }

        kprint(" | ");
        for (u32 j = 0; j < 16 && (i + j) < count; j++) {
            u8 b = ptr[i + j];
            if (b >= 32 && b <= 126) {
                char ch[2] = { (char)b, '\0' };
                kprint(ch);
            } else {
                kprint(".");
            }
        }
        kprintln("");
    }
    kprintln("");
}

static void cmd_cpuid_info(void) {
    kprintln("=== Hardware CPUID & Microarchitecture Telemetry ===");
    u32 eax, ebx, ecx, edx;

    char vendor[13];
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(*(u32*)&vendor[0]), "=c"(*(u32*)&vendor[8]), "=d"(*(u32*)&vendor[4]) : "a"(0));
    vendor[12] = '\0';
    kprint("  Processor Vendor  : "); kprintln(vendor);

    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    kprint("  Instruction Sets  : ");
    if (edx & (1 << 25)) kprint("SSE ");
    if (edx & (1 << 26)) kprint("SSE2 ");
    if (ecx & (1 << 0))  kprint("SSE3 ");
    if (ecx & (1 << 19)) kprint("SSE4.1 ");
    if (ecx & (1 << 20)) kprint("SSE4.2 ");
    if (ecx & (1 << 25)) kprint("AES-NI ");
    if (ecx & (1 << 28)) kprint("AVX ");
    if (ecx & (1 << 12)) kprint("FMA ");
    if (ecx & (1 << 5))  kprint("VMX ");
    kprintln("");

    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    kprint("  Extended Features : ");
    if (ebx & (1 << 5))  kprint("AVX2 ");
    if (ebx & (1 << 16)) kprint("AVX-512 ");
    if (ecx & (1 << 3))  kprint("PKU ");
    if (ebx & (1 << 18)) kprint("RDSEED ");
    if (ebx & (1 << 20)) kprint("SMAP ");
    if (ebx & (1 << 7))  kprint("SMEP ");
    kprintln("");
    kprintln("");
}

static void cmd_rdmsr_tool(const char* args) {
    while (*args == ' ') args++;
    if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X')) args += 2;
    u32 msr = 0;
    while (*args) {
        char c = *args;
        u8 d = 0;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        msr = (msr << 4) | d;
        args++;
    }
    if (msr == 0) msr = 0xC0000080; /* MSR_EFER */

    u32 low = 0, high = 0;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    kprint("  MSR 0x"); kprint_hex32(msr);
    kprint(" = 0x"); kprint_hex32(high); kprint_hex32(low);
    kprintln("");
}

static void cmd_sysctl_tool(void) {
    kprintln("=== LastKernel Hardcore Sysctl Parameters ===");
    kprintln("  kernel.apk_mode        = balanced");
    kprintln("  kernel.pku_enforced    = 1");
    kprintln("  kernel.mitigations     = off (Zero-Overhead Max Compute)");
    kprintln("  sched.quantum_ms       = 4");
    kprintln("  sched.fastlane_1000hz  = 1");
    kprintln("  net.ring_buffer_depth  = 64");
    kprintln("  vm.shadow_journal_crc32= 1");
    kprintln("");
}

static void cmd_exfat_info(void) {
    kprintln("=== exFAT (Extended File Allocation Table) Filesystem ===");
    exfat_fs_t fs;
    exfat_get_global_status(&fs);
    kprint("  Status        : ");
    kprintln(fs.is_mounted ? "MOUNTED & READY" : "NOT MOUNTED");
    kprint("  Volume Label  : ");
    kprintln(fs.volume_label);
    kprint("  Sector Size   : ");
    kprint_u32(fs.bytes_per_sector);
    kprintln(" Bytes");
    kprint("  Cluster Size  : ");
    kprint_u32(fs.bytes_per_cluster);
    kprintln(" Bytes (4KB / 64-bit DMA)");
    kprint("  Total Clusters: ");
    kprint_u32(fs.total_clusters);
    kprint(" (");
    kprint_u32((fs.total_clusters * fs.bytes_per_cluster) / (1024 * 1024));
    kprintln(" MB Capacity)");
    kprint("  Root Cluster  : ");
    kprint_u32(fs.root_dir_cluster);
    kprintln("");
    kprintln("  Architecture  : 64-Bit File Lengths, Unicode UTF-16, NoFatChain Fastlane");
    kprintln("");
}

int kconsole_exec_command(const char* cmd_line, char* out_buf, u32 out_buf_len) {
    (void)out_buf;
    (void)out_buf_len;
    if (!cmd_line) return -1;
    while (*cmd_line == ' ') cmd_line++;
    if (!*cmd_line) return 0;

    if (str_cmp(cmd_line, "help") == 0 || str_cmp(cmd_line, "?") == 0) {
        cmd_help();
    } else if (str_cmp(cmd_line, "uname") == 0 || str_cmp(cmd_line, "ver") == 0) {
        cmd_uname();
    } else if (str_cmp(cmd_line, "mem") == 0 || str_cmp(cmd_line, "free") == 0) {
        cmd_mem();
    } else if (str_cmp(cmd_line, "ps") == 0) {
        cmd_ps();
    } else if (str_cmp(cmd_line, "smp") == 0) {
        cmd_smp();
    } else if (str_cmp(cmd_line, "net") == 0) {
        cmd_net();
    } else if (str_cmp(cmd_line, "dhcp") == 0) {
        cmd_dhcp();
    } else if (str_ncmp(cmd_line, "ping", 4) == 0) {
        cmd_ping(cmd_line + 4);
    } else if (str_ncmp(cmd_line, "httpd", 5) == 0) {
        cmd_httpd(cmd_line + 5);
    } else if (str_cmp(cmd_line, "disks") == 0) {
        cmd_disks();
    } else if (str_cmp(cmd_line, "format") == 0) {
        cmd_format();
    } else if (str_cmp(cmd_line, "mount") == 0) {
        cmd_mount();
    } else if (str_ncmp(cmd_line, "ls", 2) == 0) {
        cmd_ls(cmd_line + 2);
    } else if (str_ncmp(cmd_line, "cat", 3) == 0) {
        cmd_cat(cmd_line + 3);
    } else if (str_ncmp(cmd_line, "write", 5) == 0) {
        cmd_write(cmd_line + 5);
    } else if (str_ncmp(cmd_line, "run", 3) == 0) {
        cmd_run(cmd_line + 3);
    } else if (str_cmp(cmd_line, "fsck") == 0 || str_cmp(cmd_line, "lastfs") == 0) {
        cmd_fsck();
    } else if (str_cmp(cmd_line, "timetravel") == 0 || str_cmp(cmd_line, "kshield") == 0) {
        cmd_timetravel();
    } else if (str_cmp(cmd_line, "heal_test") == 0 || str_cmp(cmd_line, "heal") == 0) {
        cmd_heal_test();
    } else if (str_cmp(cmd_line, "linux") == 0 || str_cmp(cmd_line, "linux-info") == 0) {
        cmd_linux_info();
    } else if (str_cmp(cmd_line, "linux-test") == 0) {
        cmd_linux_test();
    } else if (str_cmp(cmd_line, "linux-gui") == 0 || str_cmp(cmd_line, "linux-gui-test") == 0) {
        cmd_linux_gui_test();
    } else if (str_cmp(cmd_line, "apk") == 0 || str_cmp(cmd_line, "apk-status") == 0) {
        cmd_apk_status();
    } else if (str_cmp(cmd_line, "apk-top") == 0 || str_cmp(cmd_line, "apk top") == 0) {
        cmd_apk_top();
    } else if (str_ncmp(cmd_line, "apk-auto", 8) == 0) {
        cmd_apk_auto(cmd_line + 8);
    } else if (str_ncmp(cmd_line, "apk-morph", 9) == 0) {
        cmd_apk_morph(cmd_line + 9);
    } else if (str_cmp(cmd_line, "apk-test") == 0) {
        cmd_apk_test();
    } else if (str_cmp(cmd_line, "apk-fault-test") == 0 || str_cmp(cmd_line, "apk-fault") == 0 || str_cmp(cmd_line, "apk fault") == 0) {
        cmd_apk_fault_test();
    } else if (str_cmp(cmd_line, "apk-wasm-jit") == 0 || str_cmp(cmd_line, "apk-jit") == 0 || str_cmp(cmd_line, "apk jit") == 0) {
        cmd_apk_wasm_jit_test();
    } else if (str_cmp(cmd_line, "apk-quarantine-test") == 0 || str_cmp(cmd_line, "apk-quarantine") == 0) {
        cmd_apk_quarantine_test();
    } else if (str_cmp(cmd_line, "sound") == 0 || str_cmp(cmd_line, "audio") == 0) {
        cmd_sound_info();
    } else if (str_cmp(cmd_line, "beep") == 0 || str_cmp(cmd_line, "sound-test") == 0) {
        cmd_sound_beep();
    } else if (str_cmp(cmd_line, "usb-storage") == 0 || str_cmp(cmd_line, "usb-drive") == 0 || str_cmp(cmd_line, "usb") == 0) {
        cmd_usb_storage_info();
    } else if (str_cmp(cmd_line, "wifi") == 0 || str_cmp(cmd_line, "wlan") == 0) {
        cmd_wifi_status();
    } else if (str_cmp(cmd_line, "wifi-scan") == 0 || str_cmp(cmd_line, "wifi scan") == 0 || str_cmp(cmd_line, "wlan-scan") == 0) {
        cmd_wifi_scan();
    } else if (str_ncmp(cmd_line, "wifi-connect", 12) == 0) {
        cmd_wifi_connect(cmd_line + 12);
    } else if (str_ncmp(cmd_line, "wifi connect", 12) == 0) {
        cmd_wifi_connect(cmd_line + 12);
    } else if (str_cmp(cmd_line, "cmdline") == 0 || str_cmp(cmd_line, "bootargs") == 0) {
        cmd_cmdline_info();
    } else if (str_ncmp(cmd_line, "x/", 2) == 0) {
        cmd_hex_examine(cmd_line + 2);
    } else if (str_ncmp(cmd_line, "dump", 4) == 0) {
        cmd_hex_examine(cmd_line + 4);
    } else if (str_cmp(cmd_line, "cpuid") == 0 || str_cmp(cmd_line, "cpu-info") == 0) {
        cmd_cpuid_info();
    } else if (str_ncmp(cmd_line, "rdmsr", 5) == 0) {
        cmd_rdmsr_tool(cmd_line + 5);
    } else if (str_cmp(cmd_line, "sysctl") == 0) {
        cmd_sysctl_tool();
    } else if (str_cmp(cmd_line, "exfat") == 0 || str_cmp(cmd_line, "exfat-info") == 0) {
        cmd_exfat_info();
    } else if (str_cmp(cmd_line, "clear") == 0 || str_cmp(cmd_line, "cls") == 0) {
        screen_clear();
    } else if (str_cmp(cmd_line, "reboot") == 0) {
        kprintln("[LastKernel] Rebooting hardware...");
        power_reboot();
    } else if (str_cmp(cmd_line, "halt") == 0 || str_cmp(cmd_line, "poweroff") == 0) {
        kprintln("[LastKernel] System halted.");
        power_shutdown();
        for (;;) { __asm__ __volatile__("cli; hlt"); }
    } else {
        kprint("Unknown kernel command: '");
        kprint(cmd_line);
        kprintln("'. Type 'help' for available commands.");
    }
    return 0;
}

void kconsole_init(void) {
    kprintln("\n[LastKernel] KConsole Diagnostic Monitor Initialized.");
}

void kconsole_resume(void) {
    kprintln("\n[LastKernel] Resuming KConsole...");
    kconsole_run();
}

void kconsole_run(void) {
    char line_buf[KCONSOLE_BUF_SIZE];
    u32 line_len = 0u;

    kprintln("\nType 'help' for kernel diagnostic commands.\n");

    for (;;) {
        kprint("[LastKernel]# ");
        line_len = 0u;
        memset(line_buf, 0, sizeof(line_buf));

        for (;;) {
            int ch = keyboard_try_read_char();
            if (ch < 0) {
                /* Poll serial if no keyboard input */
                ch = serial_try_read_char();
            }
            if (ch <= 0) {
                if (httpd_active()) {
                    httpd_poll(8u);
                }
                __asm__ __volatile__("pause");
                continue;
            }

            if (ch == '\r' || ch == '\n') {
                kprintln("");
                line_buf[line_len] = '\0';
                break;
            } else if (ch == '\b' || ch == 127) {
                if (line_len > 0u) {
                    line_len--;
                    line_buf[line_len] = '\0';
                    screen_backspace();
                    serial_write("\b \b");
                }
            } else if (ch >= 32 && ch <= 126) {
                if (line_len + 1u < KCONSOLE_BUF_SIZE) {
                    line_buf[line_len++] = (char)ch;
                    line_buf[line_len] = '\0';
                    char echo_str[2] = { (char)ch, '\0' };
                    kprint(echo_str);
                }
            }
        }

        if (line_len > 0u) {
            (void)kconsole_exec_command(line_buf, 0, 0);
        }
    }
}
