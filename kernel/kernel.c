#include "kernel.h"
#include "core.h"
#include "shell.h"
#include "bootinfo.h"
#include "kernel_api.h"
#include "io.h"
#include "serial.h"
#include "userdb.h"

void stack_protector_init(void);

void kmain(void) {
    stack_protector_init();

    outb(0xE9, 'K');

    /* Initialize all monolithic kernel subsystems:
       - 64-Bit x86_64 GDT, IDT, Interrupts, SMP APIC, HPET
       - Virtual Memory, Buddy Frame Allocator, Slab Cache, VMA, Hash-LRU Page Cache
       - VFS, DevFS, Ext2, FAT32, Mounts, Disks (NVMe, AHCI, IDE, VirtIO, USB MSC)
       - Network Stack (Ethernet, IPv4/IPv6, TCP, UDP, DHCP, DNS, e1000e, RTL8169)
       - Drivers (Keyboard, Mouse, Touchpad, Printer, Serial, Framebuffer)
       - Preemptive Multicore Scheduler & POSIX/Linux Syscall Interface
    */
    core_init();
    userdb_init();

    serial_writeln("=================================================");
    serial_writeln("  LastKernel — Standalone Pure 64-Bit Monolith   ");
    serial_writeln("  Architecture: x86_64 SMP (Preemptive Kernel)   ");
    serial_writeln("=================================================");

    serial_writeln("[LastKernel] Initializing kernel debug & diagnostics shell...");
    shell_init();
    shell_run();

    /* Preemptive Multicore Idle Loop */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
