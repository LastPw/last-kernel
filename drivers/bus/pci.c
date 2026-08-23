#include "pci.h"
#include "io.h"
#include "hw_topology.h"
#include "serial.h"

/* Legacy PCI configuration via I/O ports */
#define PCI_CONFIG_ADDR 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu

/* PCIe MMCONFIG */
static u32 g_mmconfig_base = 0u;
static u8  g_mmconfig_end_bus = 0u;
static u8  g_mmconfig_ready = 0u;

/* ==================== Legacy PCI (I/O Ports) ==================== */

u32 pci_config_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = 0x80000000u |
                  ((u32)bus << 16) |
                  ((u32)(slot & 0x1Fu) << 11) |
                  ((u32)(func & 0x07u) << 8) |
                  (u32)(offset & 0xFCu);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    u32 address = 0x80000000u |
                  ((u32)bus << 16) |
                  ((u32)(slot & 0x1Fu) << 11) |
                  ((u32)(func & 0x07u) << 8) |
                  (u32)(offset & 0xFCu);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

/* ==================== PCIe MMCONFIG ==================== */

int pcie_mmconfig_init(void) {
    u32 base = hw_topology_mcfg_base();
    u32 end_bus = hw_topology_mcfg_end_bus();

    if (base == 0u || base >= 0x04000000u) {
        serial_writeln("[PCIe] MMCONFIG unavailable (no MCFG table)");
        return -1;
    }

    g_mmconfig_base = base;
    g_mmconfig_end_bus = (u8)(end_bus & 0xFFu);
    g_mmconfig_ready = 1u;

    serial_write("[PCIe] MMCONFIG enabled at 0x");
    serial_write_hex(base);
    serial_write(" end_bus=");
    serial_write_dec(end_bus);
    serial_writeln("");

    return 0;
}

int pcie_available(void) {
    return g_mmconfig_ready ? 1 : 0;
}

static inline u32* pcie_ecam_ptr(u8 bus, u8 slot, u8 func, u8 offset) {
    /* ECAM: base + (bus << 20) + (slot << 15) + (func << 12) + offset */
    return (u32*)(g_mmconfig_base +
                  ((u32)bus << 20u) +
                  ((u32)(slot & 0x1Fu) << 15u) +
                  ((u32)(func & 0x07u) << 12u) +
                  (u32)(offset & 0xFCu));
}

u32 pcie_config_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    if (!g_mmconfig_ready) return 0xFFFFFFFFu;
    return *(volatile u32*)pcie_ecam_ptr(bus, slot, func, offset);
}

void pcie_config_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    if (!g_mmconfig_ready) return;
    *(volatile u32*)pcie_ecam_ptr(bus, slot, func, offset) = value;
}

/* ==================== Unified PCI access ==================== */

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    if (g_mmconfig_ready && bus <= g_mmconfig_end_bus) {
        return pcie_config_read32(bus, slot, func, offset);
    }
    return pci_config_read32(bus, slot, func, offset);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    if (g_mmconfig_ready && bus <= g_mmconfig_end_bus) {
        pcie_config_write32(bus, slot, func, offset, value);
    } else {
        pci_config_write32(bus, slot, func, offset, value);
    }
}

/* ==================== Device discovery ==================== */

static u32 pci_read_with_fallback(u8 bus, u8 slot, u8 func, u8 offset) {
    return pci_read32(bus, slot, func, offset);
}

int pci_find_device(u16 vendor, u16 device, pci_device_t* out) {
    u32 max_bus = g_mmconfig_ready ? (u32)(g_mmconfig_end_bus + 1u) : 1u;
    for (u32 bus = 0u; bus < max_bus; bus++) {
        for (u32 slot = 0u; slot < 32u; slot++) {
            for (u32 func = 0u; func < 8u; func++) {
                u32 id = pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0u);
                if ((u16)(id & 0xFFFFu) != vendor || (u16)(id >> 16) != device) continue;
                if (out) {
                    out->bus = (u8)bus;
                    out->slot = (u8)slot;
                    out->func = (u8)func;
                    out->vendor = vendor;
                    out->device = device;
                    out->irq = (u8)(pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0x3Cu) & 0xFFu);
                    out->bar0 = pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0x10u);
                }
                return 0;
            }
        }
    }
    return -1;
}

int pci_find_class(u8 class_code, u8 subclass, u8 prog_if, pci_device_t* out) {
    u32 max_bus = g_mmconfig_ready ? (u32)(g_mmconfig_end_bus + 1u) : 1u;
    for (u32 bus = 0u; bus < max_bus; bus++) {
        for (u32 slot = 0u; slot < 32u; slot++) {
            for (u32 func = 0u; func < 8u; func++) {
                u32 id = pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0u);
                u32 class_reg;
                if ((u16)(id & 0xFFFFu) == 0xFFFFu) continue;
                class_reg = pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0x08u);
                if ((u8)(class_reg >> 24u) != class_code ||
                    (u8)(class_reg >> 16u) != subclass ||
                    (prog_if != 0xFFu && (u8)(class_reg >> 8u) != prog_if)) continue;
                if (out) {
                    out->bus = (u8)bus;
                    out->slot = (u8)slot;
                    out->func = (u8)func;
                    out->vendor = (u16)(id & 0xFFFFu);
                    out->device = (u16)(id >> 16u);
                    out->irq = (u8)(pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0x3Cu) & 0xFFu);
                    out->bar0 = pci_read_with_fallback((u8)bus, (u8)slot, (u8)func, 0x10u);
                }
                return 0;
            }
        }
    }
    return -1;
}

int pci_enable_bus_master(const pci_device_t* dev) {
    if (!dev) return -1;
    u32 command = pci_read_with_fallback(dev->bus, dev->slot, dev->func, 0x04u);
    command |= 0x00000007u; /* I/O space + memory space + bus mastering */
    pci_write32(dev->bus, dev->slot, dev->func, 0x04u, command);
    return 0;
}
