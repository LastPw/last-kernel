#include "kernel.h"
#include "core.h"
#include "kconsole.h"
#include "bootinfo.h"
#include "kernel_api.h"
#include "io.h"
#include "serial.h"
#include "userdb.h"

#include "apk.h"

void stack_protector_init(void);

void kmain(void) {
    stack_protector_init();

    outb(0xE9, 'K');

    /* Initialize all monolithic kernel subsystems:
       - 64-Bit x86_64 GDT, IDT, Interrupts, SMP APIC, HPET
       - Virtual Memory, Buddy Frame Allocator, Slab Cache, VMA, Hash-LRU Page Cache
       - VFS, DevFS, Ext2, FAT32, ISO9660, LastFS 2.0, Disks (NVMe, AHCI, IDE, VirtIO, USB MSC)
       - Network Stack (Ethernet, IPv4/IPv6, TCP, UDP, DHCP, DNS, WireGuard, e1000e, RTL8169)
       - Drivers (Keyboard, Mouse, Touchpad, Printer, Serial, Framebuffer, AC97 Audio)
       - Preemptive Multicore Scheduler & POSIX/Linux Syscall Interface
       - Adaptive Polymorphic Kernel Subsystem (APK)
    */
    core_init();
    userdb_init();
    apk_init();

    serial_writeln("=================================================");
    serial_writeln("  LastKernel — Adaptive Polymorphic Architecture ");
    serial_writeln("  Architecture: x86_64 SMP (Preemptive APK)      ");
    serial_writeln("=================================================");

    serial_writeln("[LastKernel] Starting kernel diagnostic & debug console...");
    kconsole_init();
    kconsole_run();

    /* Preemptive Multicore Idle Loop */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
