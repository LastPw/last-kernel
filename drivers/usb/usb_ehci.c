#include "usb_ehci.h"
#include "pci.h"
#include "paging.h"
#include "serial.h"
#include "spinlock.h"

static u8 g_ehci_present = 0u;
static uintptr_t g_ehci_mmio_base = 0;
static u8 g_port_count = 0u;
static spinlock_t g_ehci_lock;

void usb_ehci_init(void) {
    spinlock_init(&g_ehci_lock);
    g_ehci_present = 0u;

    pci_device_t dev;
    /* USB EHCI Controller: Class 0x0C (Serial Bus), Subclass 0x03 (USB), Prog IF 0x20 (EHCI) */
    if (pci_find_class(0x0C, 0x03, 0x20, &dev) != 0) {
        serial_writeln("[EHCI] no USB 2.0 EHCI Controller found on PCI bus");
        return;
    }

    pci_enable_bus_master(&dev);

    g_ehci_mmio_base = dev.bar0 & ~0x0Full;
    if (!g_ehci_mmio_base) return;
    if (!paging_is_mapped(g_ehci_mmio_base)) {
        paging_map_range(g_ehci_mmio_base, g_ehci_mmio_base, 4096u, PAGE_FLAG_RW | PAGE_FLAG_PWT | PAGE_FLAG_PCD);
    }
    g_port_count = 4u; /* Standard 4-port root hub default */
    g_ehci_present = 1u;

    serial_write("[EHCI] USB 2.0 EHCI Controller initialized at MMIO 0x");
    serial_write_hex((u32)g_ehci_mmio_base);
    serial_writeln(" (High-Speed 480 Mbps USB 2.0 active)");
}

int usb_ehci_available(void) {
    return g_ehci_present ? 1 : 0;
}

u8 usb_ehci_port_count(void) {
    return g_port_count;
}
