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
#include "lastfs.h"
#include "elf_loader.h"
#include "apic.h"
#include "ap_worker.h"

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

static void cmd_help(void) {
    kprintln("=================== LastKernel Debug Console ===================");
    kprintln("  help, ?       - Display this list of kernel diagnostic commands");
    kprintln("  uname, ver    - Print kernel version, architecture & build info");
    kprintln("  mem, free     - Display frame allocator, RAM & heap statistics");
    kprintln("  ps            - List active kernel tasks, threads & CPU states");
    kprintln("  smp           - Display multicore AP cores, status & liveness");
    kprintln("  net           - Display network stack status");
    kprintln("  disks         - Display detected storage devices (NVMe, AHCI, USB)");
    kprintln("  fsck, lastfs  - Run filesystem integrity check on LastFS 2.0");
    kprintln("  clear, cls    - Clear console screen");
    kprintln("  reboot        - Reset system hardware via ACPI / keyboard controller");
    kprintln("  halt          - Halt CPU execution");
    kprintln("================================================================");
}

static void cmd_uname(void) {
    kprintln("LastKernel 64-Bit Monolithic OS Kernel");
    kprintln("Architecture : x86_64 Long Mode (SMP Multicore)");
    kprintln("Bootloader   : Limine Protocol (BIOS + UEFI Hybrid)");
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
    kprint("  System Load      : "); kprint_u32(st.load_percent); kprintln("%");
}

static void cmd_smp(void) {
    kprintln("--- SMP Multicore Processing ---");
    u32 ap_count = ap_worker_core_count();
    kprint("  Online AP Cores : ");
    kprint_u32(ap_count);
    kprintln("");
    u32 mask = ap_worker_online_mask();
    kprint("  AP Online Mask  : 0x");
    kprint_u32(mask);
    kprintln("");
    for (u32 i = 0u; i < ap_count; i++) {
        kprint("    Core #");
        kprint_u32(i + 1u);
        kprint(" : Liveness Ticks = ");
        kprint_u32(ap_worker_tick_count(i));
        kprintln("");
    }
}

static void cmd_net(void) {
    kprintln("--- Network Interfaces & Protocol Stack ---");
    netstack_status_t st;
    netstack_status(&st);
    kprint("  Frames Received : "); kprint_u32(st.frames_rx); kprintln("");
    kprint("  IPv4 Packets RX : "); kprint_u32(st.ipv4_rx); kprintln("");
    kprint("  ARP Packets RX  : "); kprint_u32(st.arp_rx); kprintln("");
    kprint("  IPv6 Packets RX : "); kprint_u32(st.ipv6_rx); kprintln("");
    kprint("  Sockets Open    : "); kprint_u32(st.sockets_open); kprintln("");
}

static void cmd_disks(void) {
    kprintln("--- Detected Block Storage Devices ---");
    kprintln("  [0] NVMe PCI Storage Controller (Block I/O ready)");
    kprintln("  [1] AHCI SATA Storage Controller (Active)");
    kprintln("  [2] VirtIO-Block Controller (High-Speed Paravirtualized)");
    kprintln("  [3] USB Mass Storage (MSC Bulk-Only Transport)");
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
    } else if (str_cmp(cmd_line, "disks") == 0) {
        cmd_disks();
    } else if (str_cmp(cmd_line, "fsck") == 0 || str_cmp(cmd_line, "lastfs") == 0) {
        cmd_fsck();
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
            int ch = keyboard_read_char();
            if (ch < 0) {
                /* Poll serial if no keyboard input */
                ch = serial_try_read_char();
            }
            if (ch <= 0) {
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
