#include "core.h"
#include "gdt.h"
#include "idt.h"
#include "interrupts.h"
#include "timer.h"
#include "screen.h"
#include "keyboard.h"
#include "usb.h"
#include "mouse.h"
#include "paging.h"
#include "pci.h"
#include "frame_allocator.h"
#include "heap.h"
#include "advanced_allocator.h"
#include "slab.h"
#include "kmalloc.h"
#include "scheduler.h"
#include "task.h"
#include "devfs.h"
#include "pipe.h"
#include "pty.h"
#include "kernel_api.h"
#include "cpu_detect.h"
#include "hw_topology.h"
#include "smp.h"
#include "memory_runtime.h"
#include "power.h"
#include "metrics.h"
#include "bootinfo.h"
#include "serial.h"
#include "persist.h"
#include "clipboard.h"
#include "security.h"
#include "memory_protect.h"
#include "task_protect.h"
#include "irq_sanitizer.h"
#include "audit.h"
#include "secure_io.h"
#include "secure_patch.h"
#include "kernel_monitor.h"
#include "antivirus.h"
#include "process_monitor.h"
#include "memory_monitor.h"
#include "io_guard.h"
#include "threat_logger.h"
#include "signature_updater.h"
#include "string.h"
#include "nic.h"
#include "bluetooth.h"
#include "laptop_platform.h"
#include "numa.h"
#include "raid_server.h"
#include "server_net.h"
#include "ipmi_bmc.h"
#include "microvm.h"
#include "server_profile.h"
#include "apk.h"
#include "netstack.h"
#include "httpd.h"
#include "dhcp.h"
#include "module.h"
#include "kinit.h"
#include "sandbox.h"
#include "cgroup.h"
#include "namespace.h"
#include "io_scheduler.h"
#include "oom.h"
#include "swap.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "ai_security.h"
#include "pcspeaker.h"
#include "ac97.h"
#include "hda.h"
#include "sound.h"
#include "wifi.h"
#include "cmdline.h"
#include "rtl8169.h"

#include "touchpad.h"
#include "printer.h"
#include "ide.h"
#include "hal.h"
#include "ultimate.h"
#include "ultimate_kernel.h"
#include "pcoe2.h"
#include "security_hook.h"
#include "self_heal.h"
#include "sdc.h"
#include "compat.h"
#include "vm.h"
#include "watchdog.h"
#include "selftest.h"
#include "paniclog.h"
#include "interrupts.h"
#include "syscall.h"
#include "ktrace.h"
#include "vfs.h"
#include "file.h"
#include "driver_bus.h"
#include "disk.h"
#include "persistent_disk.h"
#include "layers.h"
#include "kshield.h"
#include "apic.h"
#include "hpet.h"
#include "ipc.h"
#include "proc_core.h"
#include "memlab.h"
#include "schedlab.h"
#include "iolab.h"
#include "seclab.h"
#include "compatlab.h"
#include "driverlab.h"
#include "fusionlab.h"
#include "fde.h"
#include "io.h"
#include "rust_bridge.h"
#include "levels.h"
#include "mount.h"
#include "rcu.h"
#include "vma.h"
#include "tpm2.h"
#include "epoll.h"
#include "virtio_gpu.h"
#include "e1000.h"
#include "ahci.h"
#include "rtl8139.h"
#include "vbe.h"
#include "gpu_drm.h"
#include "display_mst.h"
#include "shm.h"
#include "pkg_manager.h"

/* Limine (base revision >= 1) does not identity-map the lower 4 GiB, so
   MMIO windows the 32-bit era reached through identity addresses must be
   re-mapped here before any driver touches them (VGA text, VGA planes, BGA
   LFB, PCI config via 0xE0000000 window, APIC, ...).  Table pages for these
   early maps come from a dedicated pool so they never disturb the kernel
   image or the frame allocator. */
#define EARLY_MMIO_POOL_PAGES 8u
#define EARLY_MMIO_PAGE_SIZE 4096u

static u8 early_mmio_pool[EARLY_MMIO_POOL_PAGES][EARLY_MMIO_PAGE_SIZE]
    __attribute__((aligned(4096)));
static u8 early_mmio_pool_used[EARLY_MMIO_POOL_PAGES];

static u64 early_mmio_phys_of_ptr(const void* p) {
    return g_kernel_phys_base + ((u64)(uintptr_t)p - g_kernel_virt_base);
}

static u64 early_mmio_virt_of_phys(u64 phys) {
    return phys + g_hhdm_offset;
}

static int early_mmio_take_pool_page(u64* slot) {
    u32 i;
    for (i = 0; i < EARLY_MMIO_POOL_PAGES; i++) {
        if (!early_mmio_pool_used[i]) break;
    }
    if (i >= EARLY_MMIO_POOL_PAGES) return -1;
    early_mmio_pool_used[i] = 1;
    memset(&early_mmio_pool[i], 0, EARLY_MMIO_PAGE_SIZE);
    *slot = early_mmio_phys_of_ptr(&early_mmio_pool[i]);
    return 0;
}

static int early_mmio_ensure_pd(u64* pml4, u32 l4, u32 l3) {
    u64 pd_phys;
    u64* pdpt = (u64*)early_mmio_virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    if ((pdpt[l3] & PAGE_FLAG_PRESENT) == 0u) {
        if (early_mmio_take_pool_page(&pd_phys) != 0) return -1;
        pdpt[l3] = pd_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    } else if ((pdpt[l3] & PAGE_FLAG_PS) != 0u) {
        return -1; /* 1 GiB huge page blocks 4 KiB mapping */
    }
    return 0;
}

static int early_mmio_map_page(u64 va, u64 pa, u32 flags) {
    u64 cr3 = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    u64* pml4 = (u64*)early_mmio_virt_of_phys(cr3 & 0x000FFFFFFFFFF000ull);

    u32 l4 = (u32)((va >> 39) & 0x1FFu);
    u32 l3 = (u32)((va >> 30) & 0x1FFu);
    u32 l2 = (u32)((va >> 21) & 0x1FFu);
    u32 l1 = (u32)((va >> 12) & 0x1FFu);

    if ((pml4[l4] & PAGE_FLAG_PRESENT) == 0u) {
        /* Allocate a fresh PDPT for this 512 GiB window. */
        u64 pdpt_phys;
        if (early_mmio_take_pool_page(&pdpt_phys) != 0) return -1;
        pml4[l4] = pdpt_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    }
    if ((pml4[l4] & PAGE_FLAG_PS) != 0u) return -1;
    if (early_mmio_ensure_pd(pml4, l4, l3) != 0) return -1;

    u64* pdpt = (u64*)early_mmio_virt_of_phys(pml4[l4] & 0x000FFFFFFFFFF000ull);
    u64* pd = (u64*)early_mmio_virt_of_phys(pdpt[l3] & 0x000FFFFFFFFFF000ull);
    if ((pd[l2] & PAGE_FLAG_PRESENT) == 0u) {
        u64 pt_phys;
        if (early_mmio_take_pool_page(&pt_phys) != 0) return -1;
        pd[l2] = pt_phys | PAGE_FLAG_PRESENT | PAGE_FLAG_RW;
    }
    if ((pd[l2] & PAGE_FLAG_PS) != 0u) return -1;

    u64* pt = (u64*)early_mmio_virt_of_phys(pd[l2] & 0x000FFFFFFFFFF000ull);
    pt[l1] = (pa & 0x000FFFFFFFFFF000ull) | (flags & 0xFFFu) | PAGE_FLAG_PRESENT;
    __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    return 0;
}

static void early_mmio_map(void) {
    /* VGA text buffer + planes, the BGA LFB window, the PCI config window
       and the APIC base — the addresses the legacy drivers dereference
       directly.  PML4[0] is absent after Limine's base-revision >= 1. */
    (void)early_mmio_map_page(0x000B8000ull, 0x000B8000ull, PAGE_FLAG_RW);
    (void)early_mmio_map_page(0x000A0000ull, 0x000A0000ull, PAGE_FLAG_RW);
    (void)early_mmio_map_page(0x000A1000ull, 0x000A1000ull, PAGE_FLAG_RW);
    (void)early_mmio_map_page(0x01000000ull, 0x01000000ull, PAGE_FLAG_RW);
    (void)early_mmio_map_page(0xE0000000ull, 0xE0000000ull, PAGE_FLAG_RW);
    (void)early_mmio_map_page(0xFEE00000ull, 0xFEE00000ull, PAGE_FLAG_RW);
    /* HPET (kmod_arch_tables hpet_init) and the IO-APIC are outside the
       legacy 32-bit windows; both live at fixed physical addresses. */
    (void)early_mmio_map_page(0xFED00000ull, 0xFED00000ull, PAGE_FLAG_RW);
    (void)early_mmio_map_page(0xFEC00000ull, 0xFEC00000ull, PAGE_FLAG_RW);
    serial_writeln("[LastOS] early MMIO map ready");
}

extern u8 __heap_start;
extern u8 __heap_end;
#define KERNEL_HEAP_START ((void*)&__heap_start)
#define KERNEL_HEAP_SIZE  ((u32)((u64)&__heap_end - (u64)&__heap_start))

static u64 detected_ram_bytes = 0;
static u8 detected_modern_profile = 0;
static u32 detected_cores = 1;
static u8 boot_mode_requested = BOOT_MODE_DEFAULT;
static u8 boot_handcrafted_auth = 0;
static u8 boot_storage_profile = 0;
static u8 boot_net_profile = 0;
static u16 boot_flags = 0;
static u16 boot_ticks = 0;
static u16 boot_checksum = 0;
static volatile u32 panic_in_progress = 0;
static const cpu_info_t* boot_cpu = 0;

typedef struct {
    u32 valid;
    u32 int_no;
    u32 err_code;
    u32 eip;
    u32 addr_hint;
    u32 tick;
} panic_dump_t;

static panic_dump_t last_trap;
static void page_fault_handler(registers_t* regs);
static void mark_boot_success_entry(void);
static int core_use_minimal_runtime(u8 mode);
static void core_enter_minimal_runtime(u8 safe_mode, const char* reason);
static void core_init_minimal_process_runtime(void);
static int core_run_kinit_staged(void);
static void print_u32_serial(u32 value);

static void mark_boot_success_entry(void) {
    u8 idx = 0u;
    if (boot_mode_requested == BOOT_MODE_SAFE) idx = 1u;
    else if (boot_mode_requested == BOOT_MODE_FAST) idx = 2u;
    /* Menu memory slot used by PX boot menu. */
    outb(0x70, 0x80u | 0x3Eu);
    outb(0x71, 0xA5u);
    outb(0x70, 0x80u | 0x3Fu);
    outb(0x71, idx);
    /* Boot guard slot: mark successful boot so next boot is not forced to safe mode. */
    outb(0x70, 0x80u | 0x3Cu);
    outb(0x71, 0xD7u);
    outb(0x70, 0x80u | 0x3Du);
    outb(0x71, 0xA5u);
}

static int core_use_minimal_runtime(u8 mode) {
    /* Safe mode is the only intentionally reduced profile.  Command mode is
       still CLI-only at the bootloader/UI level, but needs the full memory,
       paging and process bring-up for real ring3 programs. */
    if (mode == BOOT_MODE_SAFE) return 1;
    return 0;
}

/* Command mode keeps timer scheduling disabled, but still exposes real process IDs. */
static void core_init_minimal_process_runtime(void) {
    task_init();
    task_protect_init();
    cgroup_init();
    namespace_init();
    io_scheduler_init();
    proc_core_init();
    process_monitor_init();
    scheduler_init();
    scheduler_set_core_count(1u);
    serial_writeln("[LastOS] command PID runtime ready");
}

static void core_enter_minimal_runtime(u8 safe_mode, const char* reason) {
    gdt_init();
    idt_init();
    interrupts_init();
    interrupts_set_minimal_mode(1u);
    timer_init(50);
    timer_runtime_set_features(0u);
    timer_runtime_set_ready(0u);
    core_init_minimal_process_runtime();
    keyboard_init();
    secure_io_init();
    io_guard_init();
    mouse_init();
    interrupts_mask_all();
    interrupts_unmask_irq(0); /* timer drives DHCP and network timeouts */
    interrupts_unmask_irq(1); /* keyboard */
    interrupts_unmask_irq(12); /* mouse */
    interrupts_enable();

    {
        /* Recovery also needs the real persistent VFS; it is not a RAM-only shell. */
        vfs_init();
        kfile_init();
        mount_init();
        disk_init();
        if (usb_init() == 0) {
            serial_writeln("[LastOS] command USB OHCI ready");
        } else {
            serial_writeln("[LastOS] command USB OHCI unavailable");
        }
        if (pdisk_init() == 0) {
            persist_load_disk();
            vfs_load_disk();
            {
                int recovery = vfs_recover_critical();
                if (recovery > 0) serial_writeln("[LastOS] recovery: protected VFS snapshot restored");
                else if (recovery < 0) serial_writeln("[LastOS] recovery: protected VFS restore failed");
            }
            paniclog_load_persistent();
            serial_writeln("[LastOS] command persistent disk ready");
        } else {
            serial_writeln("[LastOS] command disk unavailable; VFS is volatile");
        }
        nic_init();
        netstack_init();
        if (dhcp_configure(30u) == 0) {
            serial_writeln("[LastOS] command DHCP ready");
        } else {
            serial_writeln("[LastOS] command DHCP unavailable");
        }
        if (safe_mode) {
            vfs_fsck_report_t recovery_fsck;
            int fsck_rc = vfs_fsck(&recovery_fsck, 1u);
            if (vfs_fsck(&recovery_fsck, 0u) != 0) {
                serial_writeln("[LastOS] recovery: VFS repair incomplete");
            } else if (fsck_rc != 0) {
                serial_writeln("[LastOS] recovery: VFS repaired");
            } else {
                serial_writeln("[LastOS] recovery: VFS clean");
            }
        }
    }

    if (safe_mode) {
        screen_writeln("LastOS SAFE MINIMAL MODE");
        screen_writeln("Heavy modules are skipped.");
        screen_writeln("Shell mode is available for recovery.");
        serial_writeln("[LastOS] SAFE MINIMAL MODE");
    } else {
        screen_writeln("LastOS MINIMAL STABLE RUNTIME");
        screen_writeln("Desktop shell/gui path enabled.");
        screen_writeln("Heavy modules are deferred.");
        serial_writeln("[LastOS] MINIMAL STABLE RUNTIME");
    }
    if (reason && reason[0]) {
        serial_write("[LastOS] fallback reason: ");
        serial_writeln(reason);
    }
    serial_writeln("[LastOS] heavy modules skipped");
    /* A minimal Command/Safe boot is still a successful boot. */
    mark_boot_success_entry();
}

static int core_run_kinit_staged(void) {
    for (u32 lvl = 0u; lvl < (u32)KINIT_LVL_MAX; lvl++) {
        serial_write("[LastOS] dbg: kinit level ");
        print_u32_serial(lvl);
        serial_putc('\n');
        int rc = kinit_run_level((kinit_level_t)lvl);
        if (rc == 0) continue;

        if (lvl <= (u32)KINIT_LVL_SCHED) {
            serial_write("[LastOS] kinit critical level failed: ");
            print_u32_serial(lvl);
            serial_putc('\n');
            return -1;
        }

        boot_flags |= BOOTINFO_FLAG_SAFE_FALLBACK;
        serial_write("[LastOS] kinit non-critical level degraded: ");
        print_u32_serial(lvl);
        serial_putc('\n');
        if (lvl == (u32)KINIT_LVL_RUNTIME) {
            return -1;
        }
    }
    return 0;
}

static int drv_ok_probe(void) { return 0; }
static int drv_usb_probe(void) { return usb_init(); }
static int drv_ok_start(void) { return 0; }
static int drv_ok_stop(void) { return 0; }
static int drv_no_ioctl(u32 cmd, u32 arg) { (void)cmd; (void)arg; return 0; }

static void init_builtin_drivers(void) {
    driver_ops_t ops;
    driver_ops_t usb_ops;
    ops.probe = drv_ok_probe;
    ops.start = drv_ok_start;
    ops.stop = drv_ok_stop;
    ops.read = 0;
    ops.write = 0;
    ops.ioctl = drv_no_ioctl;
    usb_ops = ops;
    usb_ops.probe = drv_usb_probe;

    (void)driver_register("screen", &ops);
    (void)driver_register("keyboard", &ops);
    (void)driver_register("timer", &ops);
    (void)driver_register("nic", &ops);
    (void)driver_register("usb", &usb_ops);
    (void)driver_set_flags("screen", 1u, 1u);
    (void)driver_set_flags("keyboard", 1u, 1u);
    (void)driver_set_flags("timer", 1u, 1u);
    (void)driver_set_flags("nic", 1u, 0u);
    (void)driver_set_flags("usb", 1u, 0u);
    (void)driver_probe_all();
    (void)driver_start("screen");
    (void)driver_start("keyboard");
    (void)driver_start("timer");
    (void)driver_start("nic");
    (void)driver_start("usb");
}

static void init_layer_architecture(void) {
    layers_init();

    /* Kernel layer: dominant core subsystems. */
    (void)layers_register("core", LAYER_KERNEL, 16u);
    (void)layers_register("paging", LAYER_KERNEL, 10u);
    (void)layers_register("memory", LAYER_KERNEL, 10u);
    (void)layers_register("scheduler", LAYER_KERNEL, 10u);
    (void)layers_register("interrupts", LAYER_KERNEL, 8u);
    (void)layers_register("security", LAYER_KERNEL, 10u);
    (void)layers_register("compat", LAYER_KERNEL, 8u);
    (void)layers_register("resource_engine", LAYER_KERNEL, 8u);

    /* Driver layer. */
    (void)layers_register("screen_driver", LAYER_DRIVER, 4u);
    (void)layers_register("keyboard_driver", LAYER_DRIVER, 4u);
    (void)layers_register("timer_driver", LAYER_DRIVER, 4u);
    (void)layers_register("nic_driver", LAYER_DRIVER, 4u);
    (void)layers_register("usb_driver", LAYER_DRIVER, 4u);
    (void)layers_register("disk_driver", LAYER_DRIVER, 4u);

    /* Application/shell layer. */
    (void)layers_register("shell", LAYER_APPLICATION, 4u);
}

static void core_security_hook(u32 event, u32 arg0, u32 arg1) {
    if (event == HOOK_EVT_CRASH) {
        threat_logger_log(THREAT_HEAP_CORRUPT, arg0, arg1, 0);
    } else if (event == HOOK_EVT_ALLOC) {
        if (arg1 > (512 * 1024u)) {
            threat_logger_log(THREAT_HEAP_CORRUPT, arg0, arg1, 1);
        }
    }
}

static void print_u32(u32 value) {
    char buf[16];
    int i = 0;

    if (value == 0) {
        screen_putc('0');
        return;
    }

    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        screen_putc(buf[--i]);
    }
}

static void print_u32_serial(u32 value) {
    char buf[16];
    int i = 0;

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value > 0 && i < (int)sizeof(buf) - 1) {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

static void print_hex32_serial(u32 value) {
    for (int i = 7; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xF);
        serial_putc((char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10)));
    }
}

static void print_hex64_serial(u64 value) {
    for (int i = 15; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xF);
        serial_putc((char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10)));
    }
}

static void print_hex32(u32 value) {
    for (int i = 7; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xF);
        screen_putc((char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10)));
    }
}

static void print_hex64(u64 value) {
    for (int i = 15; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xF);
        screen_putc((char)(nib < 10 ? ('0' + nib) : ('A' + nib - 10)));
    }
}

void core_capture_trap(u32 int_no, u32 err_code, u32 eip, u32 addr_hint) {
    last_trap.valid = 1u;
    last_trap.int_no = int_no;
    last_trap.err_code = err_code;
    last_trap.eip = eip;
    last_trap.addr_hint = addr_hint;
    last_trap.tick = timer_ticks();
}

static void kernel_self_test(void) {
    void* p = adv_alloc(64);
    if (!p) {
        panic("Self-test failed: allocator");
    }
    adv_free(p);

    void* f = frame_alloc();
    if (!f) {
        panic("Self-test failed: frame allocator");
    }
    frame_free(f);

    if (heap_validate_integrity() != 0) {
        panic("Self-test failed: heap integrity");
    }
}

static void enforce_boot_safety(const cpu_info_t* cpu) {
    u32 force_safe = 0u;

    if (!cpu || !cpu->has_cpuid) {
        force_safe = 1u;
    }
    if (detected_ram_bytes < (8u * 1024u * 1024u)) {
        force_safe = 1u;
    }

    if (force_safe) {
        boot_mode_requested = BOOT_MODE_SAFE;
        boot_flags |= BOOTINFO_FLAG_SAFE_FALLBACK;
        audit_log(AUDIT_EVT_BOOT_FALLBACK, boot_flags, detected_ram_bytes / 1024u);
        serial_write("[DBG] forced safe mode: cpuid=");
        serial_write_dec(cpu && cpu->has_cpuid ? 1u : 0u);
        serial_write(" ram_kb=");
        serial_write_dec(detected_ram_bytes / 1024u);
        serial_putc('\n');
    }
}

static void apply_boot_policy(void) {
    switch (boot_mode_requested) {
        case BOOT_MODE_FAST:
            resource_engine_set_profile(RE_PROFILE_DESKTOP_FULL);
            hal_set_mode(HAL_MODE_DESKTOP);
            ultimate_set_mode(ULTIMATE_MODE_DESKTOP);
            power_set_mode(POWER_MODE_PERFORMANCE);
            sdc_set_mode(SDC_MODE_DESKTOP);
            kshield_set_mode(KSHIELD_MODE_BALANCED);
            break;
        case BOOT_MODE_SAFE:
            resource_engine_set_profile(RE_PROFILE_DESKTOP_LIGHT);
            hal_set_mode(HAL_MODE_AUTO);
            ultimate_set_mode(ULTIMATE_MODE_AUTO);
            power_set_mode(POWER_MODE_LOW_POWER);
            sdc_set_mode(SDC_MODE_DESKTOP);
            kshield_set_mode(KSHIELD_MODE_HARDENED);
            kshield_set_lockdown(2u);
            break;
        case BOOT_MODE_DESKTOP:
            resource_engine_set_profile(RE_PROFILE_DESKTOP_FULL);
            hal_set_mode(HAL_MODE_DESKTOP);
            ultimate_set_mode(ULTIMATE_MODE_DESKTOP);
            power_set_mode(POWER_MODE_BALANCED);
            sdc_set_mode(SDC_MODE_DESKTOP);
            kshield_set_mode(KSHIELD_MODE_BALANCED);
            break;
        case BOOT_MODE_SERVER:
            resource_engine_set_profile(RE_PROFILE_SERVER_FULL);
            hal_set_mode(HAL_MODE_SERVER);
            ultimate_set_mode(ULTIMATE_MODE_SERVER);
            power_set_mode(POWER_MODE_PERFORMANCE);
            sdc_set_mode(SDC_MODE_SERVER);
            kshield_set_mode(KSHIELD_MODE_HARDENED);
            kshield_set_lockdown(1u);
            break;
        case BOOT_MODE_VPS:
            resource_engine_set_profile(RE_PROFILE_SERVER_LIGHT);
            hal_set_mode(HAL_MODE_SERVER);
            ultimate_set_mode(ULTIMATE_MODE_VPS);
            power_set_mode(POWER_MODE_BALANCED);
            sdc_set_mode(SDC_MODE_SERVER);
            kshield_set_mode(KSHIELD_MODE_HARDENED);
            kshield_set_lockdown(1u);
            break;
        default:
            resource_engine_set_profile(RE_PROFILE_AUTO);
            hal_set_mode(HAL_MODE_AUTO);
            ultimate_set_mode(ULTIMATE_MODE_AUTO);
            power_set_mode(POWER_MODE_AUTO);
            kshield_set_mode(KSHIELD_MODE_BALANCED);
            break;
    }
}

static u32 boot_timer_feature_mask(void) {
    u32 mask = TIMER_FEAT_SCHED | TIMER_FEAT_MONITOR;

    if (boot_mode_requested == BOOT_MODE_SAFE) {
        return mask;
    }

    mask |= TIMER_FEAT_NET | TIMER_FEAT_RESOURCE;

    if (boot_mode_requested == BOOT_MODE_DESKTOP || boot_mode_requested == BOOT_MODE_DEFAULT) {
        mask |= TIMER_FEAT_SECURITY | TIMER_FEAT_VM_COMPAT;
        return mask;
    }

    if (boot_mode_requested == BOOT_MODE_FAST) {
        /* Command boot remains text-only and uses the proven core timer set.
           Engine/scanner ticks are deferred until their initialization order
           is independently hardened. */
        mask |= TIMER_FEAT_SECURITY | TIMER_FEAT_VM_COMPAT;
        return mask;
    }

    if (boot_mode_requested == BOOT_MODE_SERVER || boot_mode_requested == BOOT_MODE_VPS) {
        mask |= TIMER_FEAT_SECURITY | TIMER_FEAT_VM_COMPAT | TIMER_FEAT_ENGINE | TIMER_FEAT_SCANNERS;
    }

    return mask;
}

static int kmod_arch_cpu(void) {
    cpu_detect_init();
    boot_cpu = cpu_get_info();
    /* paging_init() only freezes the Limine geometry into globals; it must
       run before any paging_map/query call (hw_topology maps the ACPI RSDP
       page below).  It deliberately does not build new tables in P1. */
    paging_init();
    hw_topology_init(boot_cpu);
    detected_modern_profile = boot_cpu->is_modern_profile;
    detected_cores = hw_topology_core_count();
    security_init(boot_cpu);
    {
        const security_profile_t* sp = security_profile();
        paging_set_nx_capable(sp->nx_active ? 1u : 0u);
        paging_set_wx_enforced(1u);
    }
    audit_init();
    enforce_boot_safety(boot_cpu);
    return 0;
}

static int kmod_arch_tables(void) {
    gdt_init();
    idt_init();
    interrupts_init();
    serial_writeln("[LastOS] init: interrupts");

    /* Initialize APIC (LAPIC + I/O APIC) for modern interrupt handling */
    apic_init();
    if (apic_is_available()) {
        serial_writeln("[LastOS] init: APIC enabled");
        /* Switch to APIC interrupt mode */
        interrupts_set_apic_mode(1u);
    }

    /* Initialize HPET for high-precision timing */
    hpet_init();
    if (hpet_available()) {
        serial_writeln("[LastOS] init: HPET enabled");
    }

    return 0;
}

static int kmod_mm(void) {
    frame_allocator_init(detected_ram_bytes);
    serial_write("[LastOS] frame_total=");
    print_u32_serial(frame_total());
    serial_putc('\n');
    if (frame_total() == 0u) {
        return -1;
    }
    if (boot_mode_requested != BOOT_MODE_SAFE) {
        /* Keep the pre-paging marker short; serial output is still transitioning here. */
        serial_writeln("[LastOS] paging");
        paging_init();
        serial_writeln("[LastOS] init: paging ok");
        paging_set_user_space_split(USER_VIRT_BASE, USER_VIRT_LIMIT);
        serial_writeln("[LastOS] init: vm begin");
        vm_init(detected_ram_bytes);
        serial_writeln("[LastOS] init: vm ok");
        memory_protect_init();
        serial_writeln("[LastOS] init: vm");
    } else {
        serial_writeln("[LastOS] init: vm skipped (safe mode)");
    }
    heap_init(KERNEL_HEAP_START, KERNEL_HEAP_SIZE);
    advanced_allocator_init();
    slab_init();
    kernel_self_test();
    if (boot_mode_requested != BOOT_MODE_SAFE) {
        memory_protect_guard_page((u64)(uintptr_t)KERNEL_HEAP_START + (u64)KERNEL_HEAP_SIZE);
    }
    serial_writeln("[LastOS] init: heap");
    return 0;
}

static int kmod_subsys(void) {
    vfs_init();
    kfile_init();
    devfs_init();
    pipe_init();
    pty_init();
    mount_init();
    driver_bus_init();
    init_builtin_drivers();
    init_layer_architecture();
    cgroup_init();
    namespace_init();
    io_scheduler_init();
    oom_init();
    swap_init_scaled(detected_ram_bytes);
    memory_runtime_init(detected_ram_bytes);
    resource_monitor_init();
    memlab_init();
    iolab_init();
    seclab_init();
    compatlab_init();
    driverlab_init();
    fusionlab_init();
    levels_init();
    return 0;
}

static int kmod_engine(void) {
    resource_engine_init(detected_ram_bytes, detected_cores, detected_modern_profile);
    hal_init(detected_ram_bytes, detected_cores, boot_cpu);
    ultimate_init(detected_ram_bytes, detected_cores, boot_cpu);
    pcoe2_init(detected_ram_bytes, detected_cores, boot_cpu);
    serial_writeln("[LastOS] init: engines");
    ukernel_init(boot_cpu, detected_ram_bytes, detected_cores);
    security_hook_init();
    self_heal_init();
    sdc_init();
    kshield_init();
    watchdog_init();
    syscall_init();
    selftest_init();
    return 0;
}

static int kmod_sched(void) {
    task_init();
    task_protect_init();
    task_protect_set_range(0, 0x00000000, 0x00400000, TASK_PRIV_KERNEL);
    process_monitor_init();
    scheduler_init();
    smp_init();
    /* smp_start_aps() is wired but disabled: the INIT-SIPI sequence causes
       a boot loop on QEMU -smp >1 (trampoline identity-mapping lands but
       the APs still fault).  Needs a dedicated debugging session with
       per-step serial traces through the SIPI path.
       The ap_worker infrastructure (descriptor loading, work slots,
       liveness counters) is fully implemented and ready for activation. */
    scheduler_set_core_count(1u);
    rust_bridge_init(detected_cores);
    rust_bridge_mem_init(detected_ram_bytes);
    compat_init();
    serial_writeln("[LastOS] init: scheduler");
    if (rust_bridge_available()) {
        serial_writeln("[LastOS] init: rust bridge active");
    } else {
        serial_writeln("[LastOS] init: rust bridge fallback(c)");
    }
    return 0;
}

static int kmod_services(void) {
    rcu_init();
    vma_init();
    tpm2_init();
    epoll_init();
    shm_init();
    pkg_manager_init();
    apk_init();
    keyboard_init();
    mouse_init();
    touchpad_init();
    printer_init();
    secure_io_init();
    io_guard_init();
    memory_monitor_init();
    antivirus_init();
    ai_security_init();
    signature_updater_init();
    /* Initialize PCIe MMCONFIG for memory-mapped PCI config access */
    if (pcie_mmconfig_init() == 0) {
        serial_writeln("[LastOS] init: PCIe MMCONFIG enabled");
    }

    /* Initialize Hardware Driver Suite */
    pcspeaker_init();
    e1000_init();
    rtl8139_init();
    rtl8169_init();
    ahci_init();
    ide_init();
    sound_init();
    /* Auto-boot-chirp disabled: PCI detection of the AC'97 device is
       currently flaky across boots and the post-playback path needs
       stabilisation. Use the 'audio beep/test' shell commands instead. */
    {
        u64 fb_phys = g_boot_framebuffer_phys;
        u64 fb_virt = fb_phys ? (fb_phys + g_hhdm_offset) : 0;
        vbe_init(fb_phys, (u32)g_boot_framebuffer_w, (u32)g_boot_framebuffer_h, (u32)g_boot_framebuffer_pitch, (u16)g_boot_framebuffer_bpp);
        gfx_set_boot_framebuffer(fb_phys, fb_virt, (u32)g_boot_framebuffer_w, (u32)g_boot_framebuffer_h, (u32)g_boot_framebuffer_pitch, (u16)g_boot_framebuffer_bpp);
    }
    virtio_gpu_init();
    gpu_drm_init();
    display_mst_init();

    nic_init();
    wifi_init();
    bluetooth_init();
    bt_intel_init((void*)0);
    bt_realtek_init((void*)0);
    acpi_ec_init();
    battery_sbs_init();
    laptop_vendors_init();
    backlight_als_init();
    cpu_power_init();
    numa_init();
    raid_server_init();
    server_net_init();
    ipmi_bmc_init();
    microvm_subsystem_init();
    server_profile_init();
    /* IPv6 link-local and NDP state depend on the NIC MAC being ready. */
    netstack_init();
    serial_write("[LastOS] NIC RTL8139 ready=");
    print_u32_serial((u32)nic_ready());
    serial_putc('\n');

    module_init();
    ipc_init();
    proc_core_init();
    {
        pcoe2_status_t ps;
        ukernel_status_t us;
        pcoe2_status(&ps);
        ukernel_status(&us);
        pcoe2_preload_predictive(ps.mode);
        ukernel_preload_profile(us.mode);
    }
    security_hook_register("security", core_security_hook);
    sandbox_init();
    serial_writeln("[LastOS] init: apps");
    return 0;
}

static int kmod_runtime(void) {
    u32 target_features = 0u;
    /* The LAPIC timer calibration polls the PIT exclusively; the timer IRQ
       must not fire mid-calibration (the vector is not installed yet anyway,
       and a pending IRQ0 would be misattributed). */
    interrupts_disable();
    timer_init(100);
    /* Re-enable interrupts only after the timer vector is fully installed;
       the I/O APIC redirection for IRQ0 was set up by apic_init() during
       arch.tables, so servicing can begin as soon as the timer is live. */
    interrupts_enable();
    if (boot_mode_requested == BOOT_MODE_SAFE) {
        timer_runtime_set_features(0u);
        timer_runtime_set_target_features(0u);
        timer_runtime_set_ready(0u);
    } else {
        /*
         * P1 bring-up: the LAPIC timer must not drive the full scheduler
         * path while disk/persistent/fs init still runs on the 16 KiB boot
         * stack.  The timer IRQ stays in tick-only mode until the late
         * bring-up section flips timer_runtime_set_ready(1).
         */
        timer_runtime_set_features(0u);
        timer_runtime_set_target_features(boot_timer_feature_mask());
        timer_runtime_set_ready(0u);
    }
    power_init(boot_cpu, 100);
    apply_boot_policy();
    irq_sanitizer_init();
    secure_patch_init();
    metrics_init(detected_ram_bytes);
    kernel_monitor_init();
    serial_writeln("[LastOS] init: monitor");
    if (boot_mode_requested != BOOT_MODE_SAFE) {
        serial_write("[LastOS] runtime target features=0x");
        print_hex32_serial(target_features);
        serial_putc('\n');
    }
    isr_register_handler(14, page_fault_handler);
    interrupts_mask_all();
    interrupts_unmask_irq(0); /* timer */
    interrupts_unmask_irq(1); /* keyboard */
    interrupts_unmask_irq(12); /* mouse */
    interrupts_enable();
    /* Persistent storage may need a virtio completion IRQ, so initialize it
       only after the runtime interrupt model is fully live. */
    /* Initialize Full Disk Encryption (FDE) subsystem */
    fde_init();
    disk_init();
    if (pdisk_init() == 0) {
        serial_writeln("[LastOS] persistent disk ready");
        paniclog_load_persistent();
    } else {
        serial_writeln("[LastOS] persistent disk unavailable; VFS is volatile");
    }
    serial_writeln("[LastOS] dbg: persist_load_disk");
    persist_load_disk();
    serial_writeln("[LastOS] dbg: vfs_load_disk");
    vfs_load_disk();
    serial_writeln("[LastOS] dbg: fs_auto_mount");
    {
        int auto_mounts = fs_auto_mount();
        serial_write("[LastOS] auto-mounted filesystems=");
        print_u32_serial(auto_mounts > 0 ? (u32)auto_mounts : 0u);
        serial_putc('\n');
    }
    serial_writeln("[LastOS] dbg: dhcp_configure");
    if (dhcp_configure(120u) == 0) {
        serial_writeln("[LastOS] DHCP network configuration ready");
    } else {
        serial_writeln("[LastOS] DHCP unavailable; using static network configuration");
    }
    /* Disk/persistent/fs bring-up is complete; the timer IRQ may now drive
       the full scheduler + monitor path without clobbering the boot stack. */
    serial_writeln("[LastOS] dbg: timer_runtime_set_ready");
    timer_runtime_set_ready(1u);
    return 0;
}

static void page_fault_handler(registers_t* regs) {
    u64 cr2 = 0;
    u64 live_rbp = 0;
    u32 task_id;
    int user_origin;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    __asm__ __volatile__("mov %%rbp, %0" : "=r"(live_rbp));
    task_id = scheduler_current_task();
    user_origin = ((regs->cs & 3u) == 3u) || scheduler_task_is_user(task_id);
    if (user_origin) {
        serial_write("[pf-live] rbp_cpu=0x");
        print_hex64_serial(live_rbp);
        serial_write(" rbp_frame=0x");
        print_hex64_serial(regs->rbp);
        serial_write(" rax_frame=0x");
        print_hex64_serial(regs->rax);
        serial_putc('\n');
        {
            /* TEMP-DEBUG: read the user's g_display + flush_cb pointers and the
               stack arg passed to the flush callback, straight from user VA. */
            u64 u_gd = 0x10000022E60ull;
            serial_write("[pf-cb] gdisp_ptrs ");
            for (u32 i = 0u; i < 8u; i++) {
                u64 val = 0;
                u64 va = u_gd + (u64)i * 8u;
                if (paging_validate_user_ptr(va, 8u, 0u)) {
                    memcpy(&val, (const void*)(uintptr_t)va, 8u);
                } else {
                    val = 0xDEADBEEFDEADBEEFull;
                }
                serial_write(" s");
                serial_write_dec(i);
                serial_write("=0x");
                serial_write_hex((u32)(val >> 32));
                serial_write_hex((u32)val);
            }
            serial_write(" rsp0x20=0x");
            {
                u64 val = 0;
                u64 va = regs->rsp + 0x20u;
                if (paging_validate_user_ptr(va, 8u, 0u)) {
                    memcpy(&val, (const void*)(uintptr_t)va, 8u);
                } else {
                    val = 0xDEADBEEFDEADBEEFull;
                }
                print_hex64_serial(val);
            }
            serial_putc('\n');
        }
        serial_write("[pf-frame] rip=0x");
        print_hex64_serial(regs->rip);
        serial_write(" rsp=0x");
        print_hex64_serial(regs->rsp);
        serial_write(" rbp=0x");
        print_hex64_serial(regs->rbp);
        serial_write(" cr2=0x");
        print_hex64_serial(cr2);
        serial_write(" err=0x");
        print_hex32_serial(regs->err_code);
        serial_write(" cs=0x");
        print_hex32_serial(regs->cs);
        serial_write(" ss=0x");
        print_hex32_serial(regs->ss);
        serial_write(" cur=");
        serial_write_dec(scheduler_current_task());
        serial_putc('\n');
        {
            /* TEMP-DEBUG: full register set + instruction bytes at rip. */
            serial_write("[pf-regs] rax=0x");
            print_hex64_serial(regs->rax);
            serial_write(" rbx=0x");
            print_hex64_serial(regs->rbx);
            serial_write(" rcx=0x");
            print_hex64_serial(regs->rcx);
            serial_write(" rdx=0x");
            print_hex64_serial(regs->rdx);
            serial_putc('\n');
            serial_write("[pf-regs] rsi=0x");
            print_hex64_serial(regs->rsi);
            serial_write(" rdi=0x");
            print_hex64_serial(regs->rdi);
            serial_write(" r8=0x");
            print_hex64_serial(regs->r8);
            serial_write(" r9=0x");
            print_hex64_serial(regs->r9);
            serial_write(" r10=0x");
            print_hex64_serial(regs->r10);
            serial_putc('\n');
            serial_write("[pf-regs] r11=0x");
            print_hex64_serial(regs->r11);
            serial_write(" r12=0x");
            print_hex64_serial(regs->r12);
            serial_write(" r13=0x");
            print_hex64_serial(regs->r13);
            serial_write(" r14=0x");
            print_hex64_serial(regs->r14);
            serial_write(" r15=0x");
            print_hex64_serial(regs->r15);
            serial_putc('\n');
            {
                /* TEMP-DEBUG: dump the running ISR stub bytes from kernel memory
                   (VMA 0xffffffff8000f590) and the user flush chain bytes, to
                   compare against the ELF. */
                u8* stub = (u8*)0xffffffff8000f590ull;
                serial_write("[pf-stub] ");
                for (u32 b = 0u; b < 24u; b++) {
                    serial_write_hex((u32)stub[b]);
                    serial_putc(' ');
                }
                serial_putc('\n');
                {
                    /* TEMP-DEBUG: dump the exact fault-site bytes from memory. */
                    u64 fs = 0x100000073A0ull;
                    if (paging_validate_user_ptr(fs, 0x40u, 0u)) {
                        u8 fb[0x40];
                        memcpy(fb, (const void*)(uintptr_t)fs, 0x40u);
                        serial_write("[pf-site73A0] ");
                        for (u32 b = 0u; b < 0x40u; b++) { serial_write_hex((u32)fb[b]); serial_putc(' '); }
                        serial_putc('\n');
                    }
                }
                {
                    u64 f0 = 0x100000044F0ull, f1 = 0x10000009380ull;
                    if (paging_validate_user_ptr(f0, 16u, 0u) &&
                        paging_validate_user_ptr(f1, 16u, 0u)) {
                        u8 fb0[16], fb1[16];
                        memcpy(fb0, (const void*)(uintptr_t)f0, 16u);
                        memcpy(fb1, (const void*)(uintptr_t)f1, 16u);
                        serial_write("[pf-flush44F0] ");
                        for (u32 b = 0u; b < 16u; b++) { serial_write_hex((u32)fb0[b]); serial_putc(' '); }
                        serial_write("  [pf-flush9380] ");
                        for (u32 b = 0u; b < 16u; b++) { serial_write_hex((u32)fb1[b]); serial_putc(' '); }
                        serial_putc('\n');
                    } else {
                        serial_writeln("[pf-flush] user flush pages NOT VALID");
                    }
                }
            }
            {
                u64 page = regs->rip & ~0xFFFull;
                u64 frame = paging_get_page_frame(page);
                serial_write("[pf-instr] page=0x");
                print_hex64_serial(page);
                serial_write(" frame=0x");
                print_hex64_serial(frame);
                if (frame != 0) {
                    u8* src = (u8*)(frame + 0xFFFF800000000000ull);
                    u32 off = (u32)(regs->rip & 0xFFFull);
                    for (u32 b = off; b < off + 32u && b < 0x1000u; b++) {
                        serial_write_hex((u32)src[b]);
                        serial_putc(' ');
                    }
                }
                serial_putc('\n');
            }
        }
    }
    int is_user_addr = (cr2 >= USER_VIRT_BASE && cr2 < USER_VIRT_LIMIT);
    if ((user_origin || is_user_addr) && task_id != 0u &&
        paging_space_current() != scheduler_task_address_space(task_id)) {
        /* A dying child can leave CR3 on the kernel map while its awakened
           parent is already the selected task. Restore the parent's map and
           retry the interrupted instruction before classifying the fault. */
        (void)paging_space_switch(scheduler_task_address_space(task_id));
    }
    if ((user_origin || is_user_addr) && elf32_handle_page_fault(task_id, cr2, regs->err_code) != 0) {
        return;
    }
    {
        u64 cr3 = 0;
        __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
        cr3 &= 0x000FFFFFFFFFF000ull;
        u64* pm = (u64*)(cr3 + 0xFFFF800000000000ull);
        {
            extern u64 paging_space_phys(void);
            serial_write("[pf-dbg] cr3=0x");
            serial_write_hex((u32)(cr3 >> 12));
            serial_write(" space_phys=0x");
            serial_write_hex((u32)(paging_space_phys() >> 12));
            serial_putc('\n');
        }
        u64 va = cr2 & ~0xFFFull;
        serial_write("[pf-dbg3] cr3=0x");
        serial_write_hex((u32)(cr3 >> 12));
        serial_write(" pm_fixed=0x");
        print_hex64_serial((u64)(uintptr_t)pm);
        serial_write(" pm_hhdm=0x");
        print_hex64_serial((u64)(uintptr_t)paging_phys_to_virt(cr3));
        serial_write(" hhdm_full=0x");
        print_hex64_serial(g_hhdm_offset);
        serial_write(" pm2=0x");
        serial_write_hex((u32)(pm[2]));
        serial_write(" pm2h=0x");
        serial_write_hex((u32)(((u64*)paging_phys_to_virt(cr3))[2]));
        serial_putc('\n');
        u64 l4e = pm[2];
        u64* pdpt = (u64*)((l4e & 0x000FFFFFFFFFF000ull) + 0xFFFF800000000000ull);
        u64 l3e = pdpt[0];
        u64* pd = (u64*)((l3e & 0x000FFFFFFFFFF000ull) + 0xFFFF800000000000ull);
        u64 l2e = pd[0];
        u64* pt = (u64*)((l2e & 0x000FFFFFFFFFF000ull) + 0xFFFF800000000000ull);
        u64 l1e = pt[0];
        serial_write("[pf-dbg] l4e=0x");
        serial_write_hex((u32)(l4e >> 12));
        serial_write(" l3e=0x");
        serial_write_hex((u32)(l3e >> 12));
        serial_write(" l2e=0x");
        serial_write_hex((u32)(l2e >> 12));
        serial_write(" l1e=0x");
        serial_write_hex((u32)(l1e >> 12));
        serial_write(" cur_space=");
        serial_write_dec(paging_space_current());
        serial_write(" err=");
        print_hex32_serial(regs->err_code);
        serial_putc('\n');
        serial_write("[pf-dbg2] real va=0x");
        serial_write_hex((u32)((cr2 & ~0xFFFull) >> 12));
        serial_write(" getflags=0x");
        serial_write_hex(paging_get_page_flags(cr2 & ~0xFFFull));
        serial_write(" l1e_full=0x");
        serial_write_hex((u32)(pt[0]));
        serial_putc('\n');
    }
    serial_write("[page-fault] err=0x");
    print_hex32_serial(regs->err_code);
    serial_write(" rip=0x");
    print_hex64_serial(regs->rip);
    serial_write(" cr2=0x");
    print_hex64_serial(cr2);
    serial_write(" task=0x");
    print_hex32_serial(task_id);
    serial_write(" space=0x");
    print_hex32_serial(paging_space_current());
    serial_putc('\n');
    audit_log(AUDIT_EVT_PAGE_FAULT, cr2, regs->err_code);
    ktrace_push(KTRACE_EVT_PAGE_FAULT, 0u, (u32)cr2, regs->err_code, (u32)regs->rip);
    core_capture_trap(14u, regs->err_code, (u32)regs->rip, (u32)cr2);

    if (memory_protect_is_guard_fault(cr2)) {
        audit_log(AUDIT_EVT_GUARD_HIT, cr2, (u32)regs->rip);
    }

    if ((regs->err_code & 0x10u) != 0u) {
        if (!paging_validate_exec_ptr(cr2)) {
            audit_log(AUDIT_EVT_TASK_VIOLATION, scheduler_current_task(), cr2);
            ktrace_push(KTRACE_EVT_PAGE_FAULT, 0u, cr2, regs->err_code, 0xEEC0DE01u);
            {
                u32 tid = task_id;
                if (user_origin && tid != 0u) {
                    (void)scheduler_kill_task(tid);
                    scheduler_schedule_from_irq(regs);
                    return;
                }
            }
            panic("Page fault: execute on NX/W^X page");
        }
    }

    vm_fault_result_t vmr = vm_handle_page_fault(scheduler_current_task(), (u32)cr2, regs->err_code, (u32)regs->rip);
    if (vmr == VM_FAULT_HANDLED) {
        return;
    }

    {
        u32 tid = task_id;
        if (user_origin && tid != 0u) {
            serial_write("[pf-guard] rbp=0x");
            print_hex64_serial(regs->rbp);
            serial_write(" cr2=0x");
            print_hex64_serial(cr2);
            serial_write(" guard=0x");
            {
                extern int elf32_guard_addr(u32 task_id, u64* out);
                u64 g = 0;
                if (elf32_guard_addr(tid, &g) == 0) {
                    print_hex64_serial(g);
                } else {
                    serial_write("----");
                }
            }
            serial_write(" task=0x");
            print_hex32_serial(tid);
            serial_putc('\n');
            audit_log(AUDIT_EVT_TASK_VIOLATION, tid, cr2);
            (void)scheduler_kill_task(tid);
            scheduler_schedule_from_irq(regs);
            return;
        }
    }

    screen_writeln("Page fault trap");
    screen_write("err=0x");
    print_hex32(regs->err_code);
    screen_write(" rip=0x");
    print_hex64(regs->rip);
    screen_putc('\n');
    screen_write("addr=0x");
    print_hex64(cr2);
    screen_putc('\n');

    if (vmr == VM_FAULT_GUARD) {
        panic("Page fault: guard page hit");
    } else if (vmr == VM_FAULT_PROTECTION) {
        panic("Page fault: protection violation");
    } else if (vmr == VM_FAULT_OOM) {
        panic("Page fault: vm out of commit");
    } else {
        panic("Page fault: invalid memory access");
    }
}

void core_init(void) {
    serial_init();
    if (bootinfo_validate() != 0) {
        serial_writeln("[LastKernel] fatal: invalid Multiboot2 boot contract");
        for (;;) { __asm__ __volatile__("cli; hlt"); }
    }
    /* Limine (base revision >= 1) clears the identity map, so the legacy
       MMIO addresses must be re-mapped before screen_init touches the VGA
       text buffer. */
    early_mmio_map();
    screen_init();
    screen_clear();
    serial_writeln("[LastOS] serial ready");
    serial_writeln("[LastOS] dbg: persist_init");
    persist_init();
    serial_writeln("[LastOS] dbg: persist done");
    clipboard_init();
    serial_writeln("[LastOS] dbg: clipboard done");
    threat_logger_init();
    serial_writeln("[LastOS] dbg: threatlog done");
    paniclog_init();
    serial_writeln("[LastOS] dbg: paniclog done");
    ktrace_init();
    serial_writeln("[LastOS] dbg: ktrace done");

    /* The stage1 checksum contract is gone; the boot mode now comes from the
       limine command line.  Memory is derived from the limine memmap. */
    boot_mode_requested = bootinfo_parse_boot_mode(bootinfo_cmdline());
    boot_storage_profile = 0u;
    boot_net_profile = 0u;
    boot_ticks = 0u;
    boot_checksum = 0u;
    boot_handcrafted_auth = 0u;
    boot_flags = 0u;

    if (g_memmap_total_low >= (4ull * 1024ull * 1024ull)) {
        detected_ram_bytes = g_memmap_total_low;
    } else {
        detected_ram_bytes = 4ull * 1024ull * 1024ull;
    }

    boot_cpu = 0;
    if (core_use_minimal_runtime(boot_mode_requested)) {
        if (boot_mode_requested == BOOT_MODE_FAST) {
            core_enter_minimal_runtime(0u, "command console profile");
        } else {
            core_enter_minimal_runtime(1u, "safe mode profile");
        }
        return;
    }

    static const kinit_module_t core_initmods[] = {
        { "arch.cpu",      KINIT_LVL_ARCH,    kmod_arch_cpu },
        { "arch.tables",   KINIT_LVL_ARCH,    kmod_arch_tables },
        { "mm.core",       KINIT_LVL_MM,      kmod_mm },
        { "subsys.core",   KINIT_LVL_SUBSYS,  kmod_subsys },
        { "engine.core",   KINIT_LVL_ENGINE,  kmod_engine },
        { "sched.core",    KINIT_LVL_SCHED,   kmod_sched },
        { "service.core",  KINIT_LVL_SERVICE, kmod_services },
        { "runtime.core",  KINIT_LVL_RUNTIME, kmod_runtime }
    };
    kinit_reset();
    if (kinit_register(core_initmods, (u32)(sizeof(core_initmods) / sizeof(core_initmods[0]))) != 0) {
        boot_flags |= BOOTINFO_FLAG_SAFE_FALLBACK;
        core_enter_minimal_runtime(0u, "kinit register failed");
        return;
    }
    if (core_run_kinit_staged() != 0) {
        boot_flags |= BOOTINFO_FLAG_SAFE_FALLBACK;
        core_enter_minimal_runtime(0u, "kinit run failed");
        return;
    }
    interrupts_set_minimal_mode(0u);

    const cpu_info_t* cpu = boot_cpu ? boot_cpu : cpu_get_info();

    screen_writeln("LastOS Kernel Initialized");
    screen_writeln("Long Mode active");
    screen_writeln("Interrupts enabled");

    screen_write("CPU vendor: ");
    screen_writeln(cpu->vendor);
    screen_write("Detected cores: ");
    print_u32(cpu->logical_cores);
    screen_putc('\n');

    screen_write("Detected RAM (KB): ");
    print_u32(detected_ram_bytes / 1024);
    screen_putc('\n');
    screen_write("Boot mode: ");
    if (boot_mode_requested == BOOT_MODE_FAST) screen_writeln("fast");
    else if (boot_mode_requested == BOOT_MODE_SAFE) screen_writeln("safe");
    else if (boot_mode_requested == BOOT_MODE_DESKTOP) screen_writeln("desktop");
    else if (boot_mode_requested == BOOT_MODE_SERVER) screen_writeln("server");
    else if (boot_mode_requested == BOOT_MODE_VPS) screen_writeln("vps");
    else screen_writeln("default");
    screen_write("Boot profile(storage/net): ");
    print_u32(boot_storage_profile);
    screen_putc('/');
    print_u32(boot_net_profile);
    screen_putc('\n');
    screen_write("Boot ticks/checksum: ");
    print_u32(boot_ticks);
    screen_putc('/');
    print_u32(boot_checksum);
    screen_putc('\n');
    screen_write("Boot handcrafted auth: ");
    print_u32(boot_handcrafted_auth ? 1u : 0u);
    screen_putc('\n');
    if (boot_flags & BOOTINFO_FLAG_SAFE_FALLBACK) {
        screen_writeln("Boot safe fallback active");
    }
    if ((boot_flags & BOOTINFO_FLAG_KERNEL_CHECKSUM_BAD) != 0u) {
        screen_writeln("Boot checksum invalid: forced safe mode");
    }
    const security_profile_t* sp = security_profile();
    screen_write("Security NX/SMEP/SMAP: ");
    screen_putc(sp->nx_active ? '1' : '0');
    screen_putc('/');
    screen_putc(sp->smep_active ? '1' : '0');
    screen_putc('/');
    screen_putc(sp->smap_active ? '1' : '0');
    screen_putc('\n');
    serial_write("[LastOS] RAM KB=");
    print_u32_serial((u32)(detected_ram_bytes / 1024));
    serial_putc('\n');
    serial_writeln("LONGMODE-OK");
    screen_write("Boot count: ");
    print_u32(0u);
    screen_putc('\n');
    mark_boot_success_entry();

    screen_writeln("Timer initialized");
}

u32 core_tick_count(void) {
    return timer_ticks();
}

u8 core_boot_mode(void) {
    return boot_mode_requested;
}

void* kalloc(size_t size) {
    u32 tid = scheduler_current_task();
    u32 memrt_pred = memory_runtime_predict(tid, (u32)size, MEMRT_TAG_TASK);
    u32 predicted = resource_engine_predict_alloc(
        tid,
        memrt_pred,
        memory_monitor_total_bytes(),
        detected_ram_bytes,
        0
    );
    if (predicted == 0) {
        return 0;
    }
    if (!cgroup_mem_allow(tid, memory_monitor_task_bytes(tid) + predicted)) {
        return 0;
    }
    void* p = adv_alloc(predicted);
    if (!p) {
        return 0;
    }
    if (heap_validate_integrity() != 0) {
        audit_log(AUDIT_EVT_HEAP_CORRUPTION, heap_corruption_count(), predicted);
        adv_free(p);
        return 0;
    }
    memory_monitor_on_alloc(p, predicted, tid);
    memory_runtime_on_alloc(p, predicted, tid, MEMRT_TAG_TASK);
    resource_engine_note_alloc(tid, predicted);
    security_hook_notify(HOOK_EVT_ALLOC, tid, predicted);
    sdc_on_alloc(tid, predicted);
    kshield_on_alloc(tid, predicted);
    swap_balance(detected_ram_bytes, memory_monitor_total_bytes());
    (void)memory_runtime_govern_pressure(detected_ram_bytes, memory_monitor_total_bytes(), heap_fragmentation_score());
    (void)oom_handle(detected_ram_bytes, memory_monitor_total_bytes());
    return p;
}

int register_interrupt(int num, void (*handler)()) {
    if (num >= 0 && num < 32) {
        isr_register_handler((u8)num, (isr_t)handler);
        return 0;
    }

    if (num >= 32 && num < 48) {
        irq_register_handler((u8)(num - 32), (isr_t)handler);
        return 0;
    }

    return -1;
}

void schedule(void) {
    scheduler_schedule();
}

u32 ksyscall(u32 nr, u64 a0, u64 a1, u64 a2, u64 a3) {
    return syscall_dispatch(nr, a0, a1, a2, a3, scheduler_current_task());
}

static inline void x86_stac(void) {
    __asm__ __volatile__(".byte 0x0f, 0x01, 0xcb" : : : "cc");
}

static inline void x86_clac(void) {
    __asm__ __volatile__(".byte 0x0f, 0x01, 0xca" : : : "cc");
}

int kcopy_from_user(void* dst, u64 user_src, u32 len) {
    if (!dst || len == 0u) {
        return -1;
    }
    u32 task_id = scheduler_current_task();
    u32 user_space = scheduler_task_address_space(task_id);
    u32 prev_space = paging_space_current();
    if (user_space != 0u && prev_space != user_space) {
        (void)paging_space_switch(user_space);
    }
    if (!memory_protect_validate_user_buffer(user_src, len, 0)) {
        u64 first = user_src & 0xFFFFFFFFFFFFF000ull;
        u64 last = (user_src + (u64)len - 1ull) & 0xFFFFFFFFFFFFF000ull;
        for (u64 page = first;; page += 4096ull) {
            if (!memory_protect_validate_user_buffer(page, 1u, 0)) {
                (void)elf32_handle_page_fault(task_id, page, 0x4u);
            }
            if (page == last) break;
        }
        if (!memory_protect_validate_user_buffer(user_src, len, 0)) {
            if (user_space != 0u && prev_space != user_space) (void)paging_space_switch(prev_space);
            return -1;
        }
    }
    u8* d = (u8*)dst;
    const u8* s = (const u8*)user_src;
    x86_stac();
    for (u32 i = 0; i < len; i++) {
        d[i] = s[i];
    }
    x86_clac();
    if (user_space != 0u && prev_space != user_space) {
        (void)paging_space_switch(prev_space);
    }
    return 0;
}

int kcopy_to_user(u64 user_dst, const void* src, u32 len) {
    if (!src || len == 0u) {
        return -1;
    }
    u32 task_id = scheduler_current_task();
    u32 user_space = scheduler_task_address_space(task_id);
    u32 prev_space = paging_space_current();
    if (user_space != 0u && prev_space != user_space) {
        (void)paging_space_switch(user_space);
    }
    if (!memory_protect_validate_user_buffer(user_dst, len, 1)) {
        u64 first = user_dst & 0xFFFFFFFFFFFFF000ull;
        u64 last = (user_dst + (u64)len - 1ull) & 0xFFFFFFFFFFFFF000ull;
        for (u64 page = first;; page += 4096ull) {
            if (memory_protect_validate_user_buffer(page, 1u, 1) &&
                !elf32_handle_page_fault(task_id, page, 0x7u)) {
                /* Already writable pages need no COW promotion. */
            } else if (!memory_protect_validate_user_buffer(page, 1u, 1) &&
                       !elf32_handle_page_fault(task_id, page, 0x7u)) {
                if (user_space != 0u && prev_space != user_space) (void)paging_space_switch(prev_space);
                return -1;
            }
            if (page == last) break;
        }
        if (!memory_protect_validate_user_buffer(user_dst, len, 1)) {
            if (user_space != 0u && prev_space != user_space) (void)paging_space_switch(prev_space);
            return -1;
        }
    }
    const u8* s = (const u8*)src;
    u8* d = (u8*)user_dst;
    x86_stac();
    for (u32 i = 0; i < len; i++) {
        d[i] = s[i];
    }
    x86_clac();
    if (user_space != 0u && prev_space != user_space) {
        (void)paging_space_switch(prev_space);
    }
    return 0;
}

u8 kpaging_wx_enforced(void) {
    return paging_wx_enforced();
}

u8 kpaging_nx_capable(void) {
    return paging_nx_capable();
}

u32 kpaging_wx_rejects(void) {
    return paging_wx_reject_count();
}

void kpaging_set_wx(u8 enabled) {
    if (kshield_lockdown() >= 2u && !enabled) {
        return;
    }
    paging_set_wx_enforced(enabled);
}

void panic(const char* message) {
    if (__sync_lock_test_and_set(&panic_in_progress, 1u) != 0u) {
        for (;;) {
            power_idle();
        }
    }
    interrupts_disable();
    screen_clear();
    screen_set_color(0x4Fu);
    screen_writeln("AXIOM WINDOWS - KERNEL PANIC");
    screen_set_color(0x0Fu);
    screen_writeln("The kernel detected an unrecoverable fault.");
    screen_writeln(message);
    screen_writeln("");
    screen_writeln("      .----------------------------.");
    screen_writeln("      |  OOPS! SOMEONE DELETED THE  |");
    screen_writeln("      |  FLOOR UNDER THE KERNEL.    |");
    screen_writeln("      '----------------------------'");
    screen_writeln("The kernel has stopped before it can make things worse.");
    if (last_trap.valid) {
        screen_write("trap int/err/eip/addr: ");
        print_u32(last_trap.int_no);
        screen_putc('/');
        print_hex32(last_trap.err_code);
        screen_putc('/');
        print_hex32(last_trap.eip);
        screen_putc('/');
        print_hex32(last_trap.addr_hint);
        screen_putc('\n');
        screen_write("trap tick: ");
        print_u32(last_trap.tick);
        screen_putc('\n');
    }
    screen_write("heap corruptions/frag%: ");
    print_u32(heap_corruption_count());
    screen_putc('/');
    print_u32(heap_fragmentation_score());
    screen_putc('\n');
    screen_write("frames used/total: ");
    print_u32(frame_used());
    screen_putc('/');
    print_u32(frame_total());
    screen_putc('\n');
    {
        watchdog_status_t ws;
        watchdog_status(&ws);
        screen_write("watchdog stalls/heapfaults/actions/recoveries: ");
        print_u32(ws.stalls_detected);
        screen_putc('/');
        print_u32(ws.heap_faults);
        screen_putc('/');
        print_u32(ws.mitigation_actions);
        screen_putc('/');
        print_u32(ws.process_recoveries);
        screen_putc('\n');
    }
    screen_writeln("");
    screen_writeln("System halted safely. Reboot and select Recovery to inspect paniclog.");
    screen_writeln("No further kernel work will be executed.");
    serial_writeln("[LastOS] KERNEL PANIC");
    serial_writeln(message);
    {
        watchdog_status_t ws;
        watchdog_status(&ws);
        paniclog_push_for_task(scheduler_current_task(), message,
                      last_trap.valid ? last_trap.int_no : 0u,
                      last_trap.valid ? last_trap.err_code : 0u,
                      last_trap.valid ? last_trap.eip : 0u,
                      last_trap.valid ? last_trap.addr_hint : 0u,
                      timer_ticks(),
                      heap_corruption_count(),
                      heap_fragmentation_score(),
                      frame_used(),
                      frame_total(),
                      ws.stalls_detected,
                      ws.mitigation_actions);
        paniclog_persist_last();
    }
    if (last_trap.valid) {
        serial_write("[panic] trap int=");
        print_u32_serial(last_trap.int_no);
        serial_write(" err=0x");
        print_hex32_serial(last_trap.err_code);
        serial_write(" eip=0x");
        print_hex32_serial(last_trap.eip);
        serial_write(" addr=0x");
        print_hex32_serial(last_trap.addr_hint);
        serial_putc('\n');
    }
    for (;;) {
        power_idle();
    }
}

static char* console_capture_buffer = 0;
static u32 console_capture_capacity = 0u;
static u32 console_capture_length = 0u;

void kconsole_capture_begin(char* buffer, u32 capacity) {
    console_capture_buffer = buffer;
    console_capture_capacity = capacity;
    console_capture_length = 0u;
    if (buffer && capacity) buffer[0] = '\0';
}

u32 kconsole_capture_end(void) {
    u32 length = console_capture_length;
    if (console_capture_buffer && console_capture_capacity) {
        console_capture_buffer[(length < console_capture_capacity) ? length : (console_capture_capacity - 1u)] = '\0';
    }
    console_capture_buffer = 0;
    console_capture_capacity = 0u;
    console_capture_length = 0u;
    return length;
}

void kputc(char c) {
    serial_putc(c);
    /* Mirror console output to QEMU/Bochs debugcon for deterministic tests. */
    outb(0xE9u, (u8)c);
    if (console_capture_buffer && console_capture_capacity) {
        if (console_capture_length + 1u < console_capture_capacity) {
            console_capture_buffer[console_capture_length++] = c;
            console_capture_buffer[console_capture_length] = '\0';
        }
    } else if (c == '\b') {
        screen_backspace();
    } else {
        screen_putc(c);
    }
}

void kputs(const char* s) {
    if (!s) return;
    while (*s) kputc(*s++);
}

void kclear(void) {
    screen_clear();
}

void ksetcolor(u8 color) {
    screen_set_color(color);
}

u8 kgetcolor(void) {
    return screen_get_color();
}

void kresetcolor(void) {
    screen_reset_color();
}

u32 kconsole_cols(void) {
    return screen_cols();
}

u32 kconsole_rows(void) {
    return screen_rows();
}

int kgetc(void) {
    for (;;) {
        int serial_ch = serial_try_read_char();
        if (serial_ch >= 0) return serial_ch == '\r' ? '\n' : serial_ch;

        /*
         * PS/2 is the primary console input.  Command mode also initializes
         * USB, unlike Safe mode; polling USB before checking the 8042 made a
         * slow or unattached USB controller starve an otherwise ready PS/2
         * keyboard.  USB remains a fallback, then we check it once more for
         * a HID report that arrived during the poll.
         */
        int key_ch = secure_io_keyboard_try_read_char();
        if (key_ch >= 0) return key_ch;
        usb_poll();
        key_ch = secure_io_keyboard_try_read_char();
        if (key_ch >= 0) return key_ch;
        /* Keep network services alive while the interactive console waits. */
        knetstack_poll(1u);
        /* The first-login console must remain usable when a hypervisor loses
           IRQ1.  Polling here is deliberate: it also consumes real PS/2 data
           when the PIC or the controller IRQ-enable bit is unavailable. */
        __asm__ __volatile__("pause");
    }
}

int ktrygetc(void) {
    int serial_ch = serial_try_read_char();
    return serial_ch >= 0 ? serial_ch : secure_io_keyboard_try_read_char();
}

u32 kticks(void) {
    return timer_ticks();
}

u32 ktimer_frequency(void) {
    return timer_frequency();
}

int krtc_get_time(rtc_time_t* out) {
    return rtc_get_time(out);
}

u32 krtc_days_since_epoch(const rtc_time_t* time) {
    return rtc_days_since_epoch(time);
}

u32 krtc_seconds_since_midnight(const rtc_time_t* time) {
    return rtc_seconds_since_midnight(time);
}

void kentropy_fill(u8* out, u32 length) {
    entropy_fill(out, length);
}

const cpu_info_t* kcpuinfo(void) {
    return cpu_get_info();
}

void ksysinfo(kernel_metrics_t* out) {
    metrics_snapshot(out);
}

void kpersist_save(const char* text) {
    persist_save_data(text);
}

void kpersist_load(char* out, u32 out_size) {
    persist_load_data(out, out_size);
}

u32 kboot_count(void) {
    return persist_boot_count();
}

int kpersist_auth_exists(void) {
    return persist_auth_exists();
}

void kpersist_set_auth(const char* user, const char* pass) {
    persist_set_auth(user, pass);
}

int kpersist_get_auth(char* user, u32 user_size, char* pass, u32 pass_size) {
    return persist_get_auth(user, user_size, pass, pass_size);
}

int kpersist_auth_verify(const char* user, const char* pass) {
    return persist_auth_verify(user, pass);
}

void kpersist_set_hostname(const char* hostname) {
    persist_set_hostname(hostname);
}

int kpersist_get_hostname(char* out, u32 out_size) {
    return persist_get_hostname(out, out_size);
}

void kpersist_set_ui_prefs(
    u8 theme, u8 bg_preset, u8 animations, u8 high_contrast,
    u8 prompt_color, u8 input_color, u8 ok_color, u8 err_color, u8 text_color
) {
    persist_set_ui_prefs(
        theme, bg_preset, animations, high_contrast,
        prompt_color, input_color, ok_color, err_color, text_color
    );
}

int kpersist_get_ui_prefs(
    u8* theme, u8* bg_preset, u8* animations, u8* high_contrast,
    u8* prompt_color, u8* input_color, u8* ok_color, u8* err_color, u8* text_color
) {
    return persist_get_ui_prefs(
        theme, bg_preset, animations, high_contrast,
        prompt_color, input_color, ok_color, err_color, text_color
    );
}

void kclip_set(const char* text) {
    if (text) {
        antivirus_scan_buffer((const u8*)text, (u32)str_len(text), scheduler_current_task());
    }
    clipboard_set(text);
}

void kclip_get(char* out, u32 out_size) {
    clipboard_get(out, out_size);
}

int kpatch_selftest(void) {
    static const u8 ok_blob[8] = { 'L','A','S','T','O','S','1','\0' };
    patch_blob_t p;
    p.data = ok_blob;
    p.size = 8;

    u32 h = 2166136261u;
    for (u32 i = 0; i < p.size; i++) {
        h ^= p.data[i];
        h *= 16777619u;
    }
    p.checksum = h;

    int r0 = secure_patch_apply(&p);

    sig_blob_t sb;
    sb.data = ok_blob;
    sb.size = 8;
    sb.checksum = h;
    int r1 = signature_updater_apply(&sb);

    if (r0 == 0 && r1 == 0) {
        return 0;
    }
    return -1;
}


u32 kthreat_count(void) {
    return threat_logger_count();
}

int kthreat_get(u32 reverse_index, threat_record_t* out) {
    return threat_logger_get(reverse_index, out);
}

u32 kprocess_count(void) {
    return process_monitor_task_count();
}

int kprocess_get(u32 task_id, process_info_t* out) {
    return process_monitor_snapshot(task_id, out);
}

int kprocess_at(u32 index, process_info_t* out) {
    return process_monitor_snapshot_at(index, out);
}

u32 kprocess_current(void) {
    return scheduler_current_task();
}

void kantivirus_status(antivirus_status_t* out) {
    antivirus_status(out);
}

void kai_security_status(ai_security_status_t* out) {
    ai_security_status(out);
}

void khal_status(hal_status_t* out) {
    hal_status(out);
}

void khal_set_mode(hal_mode_t mode) {
    hal_set_mode(mode);
}

void khal_set_ghost_ram(u8 enabled) {
    hal_set_ghost_ram(enabled);
}

void khal_set_render_policy(hal_render_policy_t policy) {
    hal_set_render_policy(policy);
}

int khal_driver_advice(const char* module_name, hal_driver_advice_t* out) {
    return hal_driver_advice(module_name, out);
}

void kultimate_status(ultimate_status_t* out) {
    ultimate_status_get(out);
}

void kultimate_set_mode(ultimate_mode_t mode) {
    ultimate_set_mode(mode);
}

int kultimate_auth(const char* user, const char* key) {
    return ultimate_authorize_owner(user, key);
}

int kultimate_tweak(const char* key, u32 value) {
    return ultimate_apply_tweak(key, value);
}

void kultimate_rollback(void) {
    ultimate_rollback_tweaks();
}

void kkernel_status(ukernel_status_t* out) {
    ukernel_status(out);
}

void kkernel_set_mode(ukernel_mode_t mode) {
    ukernel_set_mode(mode);
}

int kkernel_auth(const char* user, const char* token) {
    return ukernel_auth_owner(user, token);
}

int kkernel_override(const char* key, u32 value) {
    return ukernel_apply_override(key, value);
}

void kkernel_rollback(void) {
    ukernel_rollback_overrides();
}

void kkernel_preload(ukernel_mode_t mode) {
    ukernel_preload_profile(mode);
}

void kpcoe2_status(pcoe2_status_t* out) {
    pcoe2_status(out);
}

void kpcoe2_set_mode(pcoe_mode_t mode) {
    pcoe2_set_mode(mode);
}

int kpcoe2_auth(const char* user, const char* token) {
    return pcoe2_auth_owner(user, token);
}

int kpcoe2_override(const char* key, u32 value) {
    return pcoe2_apply_override(key, value);
}

void kpcoe2_rollback(void) {
    pcoe2_rollback_overrides();
}

void kpcoe2_preload(pcoe_mode_t mode) {
    pcoe2_preload_predictive(mode);
}

int kcgroup_create(const char* name) {
    return cgroup_create(name);
}

int kcgroup_set_limits(u32 id, u32 cpu_percent, u32 mem_kb, u32 io_ops) {
    return cgroup_set_limits(id, cpu_percent, mem_kb, io_ops);
}

int kcgroup_bind_task(u32 task_id, u32 cgroup_id) {
    return scheduler_set_cgroup(task_id, cgroup_id);
}

u32 kcgroup_count(void) {
    return cgroup_count();
}

int kcgroup_get_by_index(u32 idx, cgroup_info_t* out) {
    return cgroup_get_by_index(idx, out);
}

int knamespace_create(void) {
    return namespace_create();
}

int knamespace_bind_task(u32 task_id, u32 ns_id) {
    return scheduler_set_namespace(task_id, ns_id);
}

u32 knamespace_count(void) {
    return namespace_count();
}

int knamespace_get_by_index(u32 idx, namespace_info_t* out) {
    return namespace_get_by_index(idx, out);
}

void kresmon(resource_snapshot_t* out) {
    resource_monitor_tick();
    resource_monitor_snapshot(out);
}

void kset_power_mode(power_mode_t mode) {
    power_set_mode(mode);
}

power_mode_t kget_power_mode(void) {
    return power_get_mode();
}

void kshutdown(void) {
    serial_writeln("[LastOS] kshutdown called");
    interrupts_disable();
    power_shutdown();
}

void kreboot(void) {
    serial_writeln("[LastOS] kreboot called");
    power_reboot();
}

int kacpi_available(void) {
    return power_acpi_available();
}

int ksched_set_priority(u32 task_id, u8 prio) {
    return scheduler_set_priority(task_id, prio);
}

int ksched_set_realtime(u32 task_id, int enabled) {
    return scheduler_set_realtime(task_id, enabled);
}

int ksched_set_class(u32 task_id, sched_class_t task_class) {
    return scheduler_set_task_class(task_id, task_class);
}

sched_class_t ksched_get_class(u32 task_id) {
    return scheduler_task_class(task_id);
}

int ksched_kill(u32 task_id) {
    return scheduler_kill_task(task_id);
}

void ksched_set_quantum(u32 level) {
    scheduler_set_quantum_level(level);
}

u32 ksched_get_quantum_level(void) {
    return scheduler_quantum_level();
}

u32 ksched_get_quantum_ticks(void) {
    return scheduler_quantum_ticks();
}

void ksched_telemetry(scheduler_telemetry_t* out) {
    scheduler_telemetry(out);
}

int ksched_task_telemetry(u32 task_id, scheduler_task_telemetry_t* out) {
    return scheduler_task_telemetry(task_id, out);
}

void ksched_policy_stats(scheduler_policy_stats_t* out) {
    scheduler_policy_stats(out);
}

int ksandbox_set_quota(const char* module_name, u32 mem_limit_kb, u32 cpu_limit_percent) {
    if (!module_name || !module_name[0]) {
        return -1;
    }
    for (u32 i = 0; i < module_count(); i++) {
        module_info_t t;
        if (module_get_by_index(i, &t) == 0 && str_cmp(t.name, module_name) == 0) {
            return sandbox_set_module_quota(i, mem_limit_kb * 1024u, cpu_limit_percent);
        }
    }
    return -1;
}

int ksandbox_get_quota(const char* module_name, u32* mem_limit_kb, u32* cpu_limit_percent) {
    if (!module_name || !module_name[0]) {
        return -1;
    }
    for (u32 i = 0; i < module_count(); i++) {
        module_info_t t;
        if (module_get_by_index(i, &t) == 0 && str_cmp(t.name, module_name) == 0) {
            sandbox_quota_t q;
            if (sandbox_get_module_quota(i, &q) != 0) {
                return -1;
            }
            if (mem_limit_kb) {
                *mem_limit_kb = q.mem_limit_bytes / 1024u;
            }
            if (cpu_limit_percent) {
                *cpu_limit_percent = q.cpu_limit_percent;
            }
            return 0;
        }
    }
    return -1;
}

void kresource_engine(resource_engine_snapshot_t* out) {
    resource_engine_snapshot(out);
}

void kresource_profile(re_profile_t profile) {
    resource_engine_set_profile(profile);
}

re_profile_t kresource_get_profile(void) {
    return resource_engine_get_profile();
}

int kmodule_load(const char* name) {
    if (kshield_authorize_module_load(name) != 0) {
        return -1;
    }
    if (sdc_authorize_module_load(name) != 0) {
        return -1;
    }
    return module_load(name);
}

int kmodule_unload(const char* name) {
    if (str_cmp(name, "security") == 0 || str_cmp(name, "resource") == 0) {
        return -1;
    }
    return module_unload(name);
}

int kmodule_install(const char* name) {
    if (kshield_authorize_module_install(name) != 0) {
        return -1;
    }
    if (sdc_authorize_module_install(name) != 0) {
        return -1;
    }
    return module_install(name);
}

int kmodule_uninstall(const char* name) {
    if (str_cmp(name, "security") == 0 || str_cmp(name, "resource") == 0) {
        return -1;
    }
    return module_uninstall(name);
}

int kmodule_mark_good(const char* name) {
    return module_mark_good(name);
}

int kmodule_rollback(const char* name) {
    return module_rollback(name);
}

int kmodule_get(const char* name, module_info_t* out) {
    return module_get(name, out);
}

u32 kmodule_count(void) {
    return module_count();
}

int kmodule_get_by_index(u32 idx, module_info_t* out) {
    return module_get_by_index(idx, out);
}

u32 khook_count(void) {
    return security_hook_count();
}

u32 kselfheal_restarts(void) {
    return self_heal_restart_count();
}

int ksandbox_open(const char* module_name, u32 mem_limit_bytes, u32 tick_budget) {
    module_info_t mi;
    if (module_get(module_name, &mi) != 0 || !mi.loaded) {
        return -1;
    }

    u32 slot = 0;
    for (u32 i = 0; i < module_count(); i++) {
        module_info_t t;
        if (module_get_by_index(i, &t) == 0 && str_cmp(t.name, module_name) == 0) {
            slot = i;
            break;
        }
    }
    return sandbox_open(slot, mem_limit_bytes, tick_budget, kticks());
}

void ksandbox_close(int token) {
    sandbox_close(token);
}

void ksdc_status(sdc_status_t* out) {
    sdc_status(out);
}

void ksdc_mode(sdc_mode_t mode) {
    sdc_set_mode(mode);
}

sdc_mode_t ksdc_get_mode(void) {
    return sdc_get_mode();
}

int ksdc_register_hook(const char* module_name, hook_fn_t fn) {
    return sdc_register_safe_hook(module_name, fn);
}

void kwatchdog_status(watchdog_status_t* out) {
    watchdog_status(out);
}

void kwatchdog_policy(watchdog_policy_t* out) {
    watchdog_get_policy(out);
}

void kwatchdog_set_policy(const watchdog_policy_t* policy) {
    watchdog_set_policy(policy);
}

int kselftest_full(selftest_result_t* out) {
    return selftest_run_full(out);
}

void kselftest_status(selftest_result_t* out) {
    selftest_status(out);
}

u32 kpaniclog_count(void) {
    return paniclog_count();
}

int kpaniclog_get(u32 reverse_index, panic_record_t* out) {
    return paniclog_get(reverse_index, out);
}

void kpaniclog_clear(void) {
    paniclog_clear();
}

u32 ktrace_events(void) {
    return ktrace_count();
}

int ktrace_event(u32 reverse_index, ktrace_event_t* out) {
    return ktrace_get(reverse_index, out);
}

u32 kvfs_list(const char* path, vfs_node_info_t* out, u32 max_out) {
    return vfs_list(path, out, max_out);
}

int kvfs_info(const char* path, vfs_node_info_t* out) {
    return vfs_info(path, out);
}

int kvfs_mkdir(const char* path) {
    return vfs_mkdir(path);
}

int kvfs_create_file(const char* path) {
    return vfs_create_file(path);
}

int kvfs_delete(const char* path) {
    return vfs_delete(path);
}

int kvfs_rename(const char* path, const char* new_name) {
    return vfs_rename(path, new_name);
}

int kvfs_read_file(const char* path, char* out, u32 out_len) {
    return vfs_read_file(path, out, out_len);
}

int kvfs_read_file_at(const char* path, u32 offset, char* out, u32 out_len) {
    return vfs_read_file_at(path, offset, out, out_len);
}

int kvfs_write_file(const char* path, const char* data, u32 len) {
    return vfs_write_file(path, data, len);
}

int kvfs_append_file(const char* path, const char* data, u32 len) {
    return vfs_append_file(path, data, len);
}

void kvfs_begin_batch(void) {
    vfs_begin_batch();
}

int kvfs_end_batch(void) {
    return vfs_end_batch();
}

int kvfs_flush_disk(void) {
    return vfs_flush_disk();
}

int kvfs_rollback_disk(void) {
    return vfs_rollback_disk();
}

int kvfs_reload_disk(void) {
    vfs_init();
    vfs_load_disk();
    return 0;
}

int kpdisk_test_torn_vfs_wal(void) {
    return pdisk_test_torn_vfs_wal();
}

int kpdisk_wal_recover(void) {
    return pdisk_wal_recover();
}

int kvfs_fsck(vfs_fsck_report_t* out, u8 repair) {
    return vfs_fsck(out, repair);
}

int kvfs_usage(u32* total_bytes, u32* used_bytes, u32* free_bytes) {
    return vfs_usage(total_bytes, used_bytes, free_bytes);
}

int kpersistent_disk_ready(void) {
    return pdisk_ready();
}

void kvfs_stats(vfs_stats_t* out) {
    vfs_stats(out);
}

void kvfs_set_credentials(u32 uid, u32 gid) {
    vfs_set_credentials(uid, gid);
}

int kvfs_set_permissions(const char* path, u16 mode) {
    return vfs_set_permissions(path, mode);
}

int kvfs_set_owner(const char* path, u32 uid, u32 gid) {
    return vfs_set_owner(path, uid, gid);
}

int kvfs_set_lock(const char* path, u8 locked) {
    return vfs_set_lock(path, locked);
}

int kvfs_access(const char* path, u8 access_mask) {
    return vfs_access(path, access_mask);
}

u32 kvfs_journal_count(void) {
    return vfs_journal_count();
}

int kvfs_journal_get(u32 reverse_index, vfs_journal_entry_t* out) {
    return vfs_journal_get(reverse_index, out);
}

u32 kdriver_count(void) {
    return driver_count();
}

int kdriver_info(u32 idx, driver_info_t* out) {
    return driver_info_by_index(idx, out);
}

int kdriver_recover(const char* name) {
    return driver_recover(name);
}

int kdriver_set_flags(const char* name, u8 sandboxed, u8 critical) {
    return driver_set_flags(name, sandboxed, critical);
}

void kdriver_bus_stats(driver_bus_stats_t* out) {
    driver_bus_stats(out);
}

int kdriver_read(const char* name, u8* out, u32 len) {
    return driver_read(name, out, len);
}

int kdriver_write(const char* name, const u8* in, u32 len) {
    return driver_write(name, in, len);
}

u32 kdisk_count(void) {
    return disk_count();
}

int kdisk_info(u32 idx, disk_info_t* out) {
    return disk_info(idx, out);
}

int kdisk_select(u32 idx) {
    return disk_select(idx);
}

int kdisk_read_sector(u32 lba, u8* out512) {
    return disk_read_sector(lba, out512);
}

int kdisk_write_sector(u32 lba, const u8* in512) {
    return disk_write_sector(lba, in512);
}

u32 kdisk_checksum(u32 start_lba, u32 count) {
    return disk_checksum_range(start_lba, count);
}

int kdisk_detect_other_os(void) {
    return disk_detect_other_os();
}

int kdisk_partition_info(disk_partition_info_t* out) {
    return disk_partition_info(out);
}

u32 kdisk_partition_count(void) {
    return disk_partition_count();
}

int kdisk_partition_get(u32 index, disk_partition_info_t* out) {
    return disk_partition_get(index, out);
}

int kdisk_free_space(disk_free_space_t* out) {
    return disk_free_space(out);
}

void kmouse_state(mouse_state_t* out) {
    mouse_state(out);
}

void klayers_status(layer_stats_t* out) {
    layers_stats(out);
}

void knetstack_status(netstack_status_t* out) {
    netstack_status(out);
}

void knetstack_poll(u32 budget) {
    netstack_poll(budget);
    httpd_poll(budget);
}

u32 knetstack_arp_count(void) {
    return netstack_arp_count();
}

int knetstack_arp_get(u32 idx, net_arp_entry_t* out) {
    return netstack_arp_get(idx, out);
}

int knetstack_arp_resolve(u32 ip, u32 timeout_ticks) {
    return netstack_arp_resolve(ip, timeout_ticks);
}

int knetstack_prepare_peer(u32 remote_ip, u32 timeout_ticks) {
    return netstack_prepare_peer(remote_ip, timeout_ticks);
}

int knetstack_ping(u32 remote_ip, u16 sequence, u32 timeout_ticks, u32* out_ticks, u8* out_ttl) {
    return netstack_ping(remote_ip, sequence, timeout_ticks, out_ticks, out_ttl);
}

int knetstack_traceroute_probe(u32 remote_ip, u8 hop_limit, u16 sequence, u32 timeout_ticks,
                               u32* out_hop_ip, u32* out_ticks, u8* out_reached) {
    return netstack_traceroute_probe(remote_ip, hop_limit, sequence, timeout_ticks,
                                     out_hop_ip, out_ticks, out_reached);
}

int knetstack_ipv6_local(u8 out_addr[16]) {
    return netstack_ipv6_local(out_addr);
}

int knetstack_ipv6_ping(const u8 remote[16], u16 sequence, u32 timeout_ticks, u32* out_ticks) {
    return netstack_ipv6_ping(remote, sequence, timeout_ticks, out_ticks);
}

u32 knetstack_route_count(void) {
    return netstack_route_count();
}

int knetstack_route_get(u32 idx, net_route_entry_t* out) {
    return netstack_route_get(idx, out);
}

int knetstack_route_add(u32 network, u32 mask, u32 gateway, u32 metric) {
    return netstack_route_add(network, mask, gateway, metric);
}

int knetstack_socket_open(u32 proto, u16 local_port, u32 remote_ip, u16 remote_port) {
    return netstack_socket_open(proto, local_port, remote_ip, remote_port);
}

int knetstack_socket_open6(u32 proto, u16 local_port, const u8 remote_ip[16], u16 remote_port) {
    return netstack_socket_open6(proto, local_port, remote_ip, remote_port);
}

int knetstack_socket_close(u32 sock_id) {
    return netstack_socket_close(sock_id);
}

int knetstack_socket_send(u32 sock_id, const u8* data, u16 len) {
    return netstack_socket_send(sock_id, data, len);
}

int knetstack_socket_recv(u32 sock_id, u8* out, u16 out_len, u32* out_src_ip, u16* out_src_port) {
    return netstack_socket_recv(sock_id, out, out_len, out_src_ip, out_src_port);
}

int knetstack_socket_recv6(u32 sock_id, u8* out, u16 out_len, u8 out_src_ipv6[16], u16* out_src_port) {
    return netstack_socket_recv6(sock_id, out, out_len, out_src_ipv6, out_src_port);
}

u32 knetstack_socket_count(void) {
    return netstack_socket_count();
}

int knetstack_socket_get(u32 idx, net_socket_info_t* out) {
    return netstack_socket_get(idx, out);
}

int knetstack_socket_state(u32 sock_id, u8* out_state) {
    return netstack_socket_state(sock_id, out_state);
}

void knetstack_set_local(u32 ip, u32 mask, u32 gateway) {
    netstack_set_local(ip, mask, gateway);
}

int kdns_resolve_ipv4(const char* name, u32* out_ip, u32 timeout_ticks) {
    return dns_resolve_ipv4(name, out_ip, timeout_ticks);
}

int kcompat_bind(u32 task_id, compat_personality_t personality, u32 bbf_seed) {
    return compat_bind_task(task_id, personality, bbf_seed);
}

void ksyscall_policy_reset(u32 task_id) {
    syscall_policy_reset(task_id);
}

int ksyscall_policy_set(u32 task_id, u32 nr, u8 allowed) {
    return syscall_policy_set(task_id, nr, allowed);
}

int kelf_load_user(const u8* image, u32 image_len, u32 task_id, elf_exec_info_t* out) {
    int load_rc;
    if (!out) return -1;
    /* ELF mapping temporarily runs with the new CR3 while copying segments.
       Do not let the timer scheduler replace that CR3 halfway through copy. */
    interrupts_disable();
    load_rc = elf32_load_user(image, image_len, task_id, out);
    interrupts_enable();
    serial_write("[elf] load rc=");
    serial_write_dec((u32)load_rc);
    serial_write(" as=");
    serial_write_dec(out ? out->address_space : 99u);
    serial_putc('\n');
    if (load_rc != 0) return -1;
    if (scheduler_set_address_space(task_id, out->address_space) != 0) {
        serial_writeln("[elf] set_address_space failed");
        (void)elf32_destroy_user(task_id);
        return -1;
    }
    serial_write("[elf] prepare entry=0x");
    serial_write_hex((u32)(out->entry >> 12));
    serial_write(" stk=0x");
    serial_write_hex((u32)(out->stack_top >> 12));
    serial_putc('\n');
    if (scheduler_prepare_user_task(task_id, out->entry, out->stack_top) != 0) {
        serial_writeln("[elf] prepare_user_task failed");
        return -1;
    }
    return 0;
}

int kelf_clone_user(u32 parent_task, u32 child_task, elf_exec_info_t* out) {
    int rc;
    if (!out || parent_task == 0u || child_task == 0u) return -1;
    interrupts_disable();
    rc = elf32_clone_user(parent_task, child_task, out);
    interrupts_enable();
    if (rc != 0) return -1;
    return scheduler_set_address_space(child_task, out->address_space);
}

int knetstack_socket_set_owner(u32 sock_id, u32 owner_task) {
    return netstack_socket_set_owner(sock_id, owner_task);
}

void knetstack_socket_release_task(u32 task_id) {
    netstack_socket_release_task(task_id);
}

int kscheduler_create_user_task(const char* name, u32* out_task_id) {
    return scheduler_create_user_task(name, out_task_id);
}

void kscheduler_request_user_switch(void) {
    scheduler_request_user_switch();
}

void ksmp_status(smp_status_t* out) {
    smp_status(out);
}

int knetstack_socket_listen(u16 local_port) { return netstack_socket_listen(local_port); }
int knetstack_socket_accept(u32 listener_id, u32* out_client_id, u32* out_remote_ip, u16* out_remote_port) {
    return netstack_socket_accept(listener_id, out_client_id, out_remote_ip, out_remote_port);
}

int ksyscall_policy_allowed(u32 task_id, u32 nr) {
    return syscall_policy_allowed(task_id, nr);
}

u32 ksyscall_policy_denied_count(u32 task_id) {
    return syscall_policy_denied_count(task_id);
}

compat_personality_t kcompat_personality(u32 task_id) {
    return compat_personality_of(task_id);
}

int kcompat_syscall(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2, u32* out_ret) {
    return compat_syscall(task_id, foreign_nr, a0, a1, a2, out_ret);
}

void kcompat_status(compat_status_t* out) {
    compat_status(out);
}

int kcompat_profile(u32 task_id, compat_profile_t* out) {
    return compat_profile(task_id, out);
}

void kcompat_note_crash(u32 task_id, u32 reason) {
    compat_note_crash(task_id, reason);
}

void kcompat_pal(compat_personality_t p, pal_descriptor_t* out) {
    compat_pal_descriptor(p, out);
}

int kcompat_sync(u32 task_id, u32 mode, compat_sync_report_t* out) {
    return compat_sync_task(task_id, mode, out);
}

u32 kcompat_autosync(u32 budget) {
    return compat_autosync_step(budget);
}

int kcompat_probe_binary(const u8* data, u32 len, compat_binary_probe_t* out) {
    return compat_probe_binary(data, len, out);
}

int kcompat_prepare_task_from_probe(u32 task_id, const compat_binary_probe_t* probe) {
    return compat_prepare_task_from_probe(task_id, probe);
}

int kcompat_prepare_task_from_kind(u32 task_id, compat_binary_kind_t kind) {
    return compat_prepare_task_from_kind(task_id, kind);
}

void kkshield_status(kshield_status_t* out) {
    kshield_status(out);
}

void kkshield_mode(kshield_mode_t mode) {
    kshield_set_mode(mode);
}

void kkshield_lockdown(u8 level) {
    kshield_set_lockdown(level);
}

u32 kframe_largest_order(void) {
    return frame_largest_free_order();
}

u32 kframe_fragmentation(void) {
    return frame_fragmentation_score();
}

u32 kslab_class_count(void) {
    return slab_class_count();
}

int kslab_class_stats(u32 idx, slab_class_stats_t* out) {
    return slab_class_stats(idx, out);
}

u32 kadv_alloc_failures(void) {
    return adv_alloc_failures();
}

int kipc_send(u32 src_task, u32 dst_task, const char* msg) {
    u32 caller = scheduler_current_task();
    u32 effective_src = src_task;
    if (!msg) return -1;
    if (caller != 0u) {
        effective_src = caller;
    }
    return ipc_send_ex(effective_src, dst_task, msg, (u32)str_len(msg), IPC_FLAG_NONE);
}

int kipc_recv(u32 dst_task, ipc_message_t* out) {
    u32 caller = scheduler_current_task();
    if (caller != 0u && dst_task != caller) {
        return -1;
    }
    return ipc_recv(dst_task, out);
}

void kipc_stats(ipc_stats_t* out) {
    ipc_stats(out);
}

void kipc_reset(void) {
    ipc_reset();
}

void kproc_register(u32 task_id, u32 parent_task) {
    proc_core_register_task(task_id, parent_task);
}

int kproc_signal_send(u32 task_id, proc_signal_t sig) {
    return proc_core_signal_send(task_id, sig);
}

int kproc_wait(u32 task_id, i32* out_exit_code) {
    return proc_core_wait(task_id, out_exit_code);
}

int kproc_wait_any(u32 parent_task, u32* out_child_task, i32* out_exit_code) {
    return proc_core_wait_any(parent_task, out_child_task, out_exit_code);
}

int kproc_has_children(u32 parent_task) {
    return proc_core_has_children(parent_task);
}

void kproc_set_parent(u32 task_id, u32 parent_task) {
    proc_core_set_parent(task_id, parent_task);
}

u32 kproc_parent(u32 task_id) {
    return proc_core_parent(task_id);
}

int kproc_is_descendant(u32 task_id, u32 ancestor_task) {
    return proc_core_is_descendant(task_id, ancestor_task);
}

int kproc_status(u32 task_id, proc_status_t* out) {
    return proc_core_status(task_id, out);
}

void kmemlab_status(memlab_snapshot_t* out) {
    memlab_snapshot(out);
}

void kmemlab_reset(void) {
    memlab_reset();
}

int kmemlab_validate(memlab_validation_t* out) {
    return memlab_validate(out);
}

int kmemlab_run(u32 scenario, u32 rounds, u32 payload, memlab_run_result_t* out) {
    return memlab_run(scenario, rounds, payload, out);
}

u32 kmemlab_hist_bins(void) {
    return memlab_histogram_bins();
}

u32 kmemlab_hist_bin(u32 idx) {
    return memlab_histogram_bin(idx);
}

void kschedlab_status(schedlab_snapshot_t* out) {
    schedlab_snapshot(out);
}

void kschedlab_reset(void) {
    schedlab_reset();
}

int kschedlab_validate(schedlab_validation_t* out) {
    return schedlab_validate(out);
}

int kschedlab_run(u32 mode, u32 rounds, schedlab_run_result_t* out) {
    return schedlab_run(mode, rounds, out);
}

void kiolab_status(iolab_snapshot_t* out) {
    iolab_snapshot(out);
}

void kiolab_reset(void) {
    iolab_reset();
}

int kiolab_validate(iolab_validation_t* out) {
    return iolab_validate(out);
}

int kiolab_run(u32 mode, u32 rounds, u32 payload, iolab_run_result_t* out) {
    return iolab_run(mode, rounds, payload, out);
}

void kseclab_status(seclab_snapshot_t* out) {
    seclab_snapshot(out);
}

void kseclab_reset(void) {
    seclab_reset();
}

int kseclab_validate(seclab_validation_t* out) {
    return seclab_validate(out);
}

int kseclab_run(u32 mode, u32 rounds, seclab_run_result_t* out) {
    return seclab_run(mode, rounds, out);
}

void kcompatlab_status(compatlab_snapshot_t* out) {
    compatlab_snapshot(out);
}

void kcompatlab_reset(void) {
    compatlab_reset();
}

int kcompatlab_validate(compatlab_validation_t* out) {
    return compatlab_validate(out);
}

int kcompatlab_run(u32 mode, u32 rounds, compatlab_run_result_t* out) {
    return compatlab_run(mode, rounds, out);
}

void kdriverlab_status(driverlab_snapshot_t* out) {
    driverlab_snapshot(out);
}

void kdriverlab_reset(void) {
    driverlab_reset();
}

int kdriverlab_validate(driverlab_validation_t* out) {
    return driverlab_validate(out);
}

int kdriverlab_run(u32 mode, u32 rounds, driverlab_run_result_t* out) {
    return driverlab_run(mode, rounds, out);
}

void kfusionlab_status(fusionlab_snapshot_t* out) {
    fusionlab_snapshot(out);
}

void kfusionlab_reset(void) {
    fusionlab_reset();
}

int kfusionlab_validate(fusionlab_validation_t* out) {
    return fusionlab_validate(out);
}

int kfusionlab_run(u32 rounds, fusionlab_run_result_t* out) {
    return fusionlab_run(rounds, out);
}

void kkinit_status(kinit_status_t* out) {
    kinit_status(out);
}

int kkinit_module_info(u32 idx, kinit_module_t* out, i32* out_rc) {
    return kinit_module_info(idx, out, out_rc);
}

void kvm_stats(vm_stats_t* out) {
    vm_stats(out);
}

void kmemrt_status(memrt_snapshot_t* out) {
    memory_runtime_snapshot(out);
}

int kmemrt_vault_store(const char* data, u32* out_handle) {
    u32 len = 0u;
    if (!data) return -1;
    while (data[len] && len < 128u) len++;
    return memory_runtime_vault_store(data, len, out_handle);
}

int kmemrt_vault_load(u32 handle, char* out, u32 out_len) {
    int n = memory_runtime_vault_load(handle, out, out_len ? out_len - 1u : 0u);
    if (n >= 0 && out && out_len) {
        out[(u32)n] = '\0';
    }
    return n;
}

int kmemrt_vault_zeroize(u32 handle) {
    return memory_runtime_vault_zeroize(handle);
}

int kmemrt_zero_copy_create(u32 kind, void* ptr, u32 len, u32* out_id) {
    if (kind > MEMRT_ZC_STORAGE) return -1;
    return memory_runtime_zero_copy_create((memrt_zero_copy_kind_t)kind, ptr, len, out_id);
}

void klevels_status(level_status_t* out) {
    levels_status(out);
}

int klevels_info(u8 level_id, level_info_t* out) {
    return levels_get_info(level_id, out);
}

int klevels_target(u8 level_id) {
    return levels_set_target(level_id);
}

void khw_topology_status(hw_topology_status_t* out) {
    hw_topology_status(out);
}

u32 khw_core_temperature(u32 core) {
    return hw_topology_core_temperature(core);
}

u32 khw_core_numa_node(u32 core) {
    return hw_topology_core_numa_node(core);
}

/* DNS Server API wrappers */
int  kdns_server_start(void) { return dns_server_start(); }
void kdns_server_stop(void) { dns_server_stop(); }
int  kdns_server_active(void) { return dns_server_active(); }
void kdns_server_status(dns_server_status_t* out) { dns_server_status(out); }

/* DHCP Server API wrappers */
int  kdhcp_server_configure(u32 pool_start, u32 pool_end, u32 subnet_mask, u32 gateway, u32 dns_server, u32 lease_ticks) {
    return dhcp_server_configure(pool_start, pool_end, subnet_mask, gateway, dns_server, lease_ticks);
}
int  kdhcp_server_start(void) { return dhcp_server_start(); }
void kdhcp_server_stop(void) { dhcp_server_stop(); }
int  kdhcp_server_active(void) { return dhcp_server_active(); }
int  kdhcp_server_lease_get(u32 index, dhcp_lease_t* out) { return dhcp_server_lease_get(index, out); }
u32  kdhcp_server_lease_count(void) { return dhcp_server_lease_count(); }
void kdhcp_server_status(dhcp_server_status_t* out) { dhcp_server_status(out); }

/* NAT API wrappers */
void knat_init(void) { nat_init(); }
int  knat_rule_add(u8 type, u8 proto, u16 external_port, u32 internal_ip, u16 internal_port, u32 internal_net, u32 internal_mask) {
    return nat_rule_add(type, proto, external_port, internal_ip, internal_port, internal_net, internal_mask);
}
int  knat_rule_del(u32 index) { return nat_rule_del(index); }
int  knat_rule_get(u32 index, nat_rule_t* out) { return nat_rule_get(index, out); }
u32  knat_rule_count(void) { return nat_rule_count(); }
void knat_set_ip_forward(u8 on) { nat_set_ip_forward(on); }
u8   knat_ip_forward(void) { return nat_ip_forward(); }
u32  knat_conntrack_count(void) { return nat_conntrack_count(); }
int  knat_conntrack_get(u32 index, conntrack_entry_t* out) { return nat_conntrack_get(index, out); }
void knat_status(nat_status_t* out) { nat_status(out); }

/* Filesystem mount API wrappers */
int kmount(const char* mount_point, const char* fstype, u32 dev_id, u32 start_lba) {
    return fs_mount(mount_point, fstype, dev_id, start_lba);
}

int kumount(const char* mount_point) {
    return fs_umount(mount_point);
}

int kformat(const char* fstype, u32 dev_id, u32 start_lba, u32 total_lba) {
    return fs_format(fstype, dev_id, start_lba, total_lba);
}

int kmount_list(mount_entry_t* out, u32 max_out) {
    return fs_mount_status(out, max_out);
}

u32 kmount_count(void) {
    return fs_mount_count();
}
