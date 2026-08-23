#include "virtio_pci.h"
#include "io.h"
#include "paging.h"
#include "serial.h"

#define VIRTIO_VENDOR 0x1AF4u
#define VIRTIO_BLK_LEGACY 0x1001u
#define VIRTIO_BLK_MODERN 0x1042u
#define PCI_CAP_PTR 0x34u
#define PCI_CAP_VENDOR 0x09u
#define VIRTIO_CAP_COMMON_CFG 1u
#define VIRTIO_CAP_NOTIFY_CFG 2u
#define VIRTIO_CAP_DEVICE_CFG 4u

#define VIRTIO_REG_STATUS 0x12u
#define VIRTIO_COMMON_STATUS 20u

static u8 cfg8(const pci_device_t* dev, u8 offset) {
    u32 value = pci_config_read32(dev->bus, dev->slot, dev->func, offset);
    return (u8)(value >> ((offset & 3u) * 8u));
}

static u32 bar_base(const pci_device_t* dev, u8 bar) {
    u32 value = pci_config_read32(dev->bus, dev->slot, dev->func,
                                  (u8)(0x10u + ((bar & 7u) * 4u)));
    return (value & 1u) ? (value & 0xFFFFFFFCu) : (value & 0xFFFFFFF0u);
}

static void map_mmio(u32 address) {
    if (address != 0u) {
        (void)paging_map_range(address & 0xFFFFF000u, address & 0xFFFFF000u,
                                4096u, PAGE_FLAG_RW | PAGE_FLAG_PWT | PAGE_FLAG_PCD);
    }
}

static int find_caps(virtio_pci_transport_t* t) {
    u8 cap = cfg8(&t->pci, PCI_CAP_PTR);
    while (cap >= 0x40u && cap < 0xFCu) {
        if (cfg8(&t->pci, cap) == PCI_CAP_VENDOR) {
            u8 type = cfg8(&t->pci, (u8)(cap + 3u));
            u8 bar = cfg8(&t->pci, (u8)(cap + 4u));
            u32 offset = pci_config_read32(t->pci.bus, t->pci.slot, t->pci.func,
                                           (u8)(cap + 8u));
            u32 address = bar_base(&t->pci, bar) + offset;
            if (type == VIRTIO_CAP_COMMON_CFG) {
                t->common_cfg = address;
                map_mmio(address);
            } else if (type == VIRTIO_CAP_NOTIFY_CFG) {
                t->notify_cfg = address;
                t->notify_multiplier = pci_config_read32(t->pci.bus, t->pci.slot,
                                                         t->pci.func, (u8)(cap + 16u));
                map_mmio(address);
            } else if (type == VIRTIO_CAP_DEVICE_CFG) {
                t->device_cfg = address;
                map_mmio(address);
            }
        }
        cap = cfg8(&t->pci, (u8)(cap + 1u));
    }
    return t->common_cfg != 0u && t->notify_cfg != 0u &&
           t->device_cfg != 0u && t->notify_multiplier != 0u;
}

int virtio_pci_probe_device(virtio_pci_transport_t* t, u16 legacy_device, u16 modern_device) {
    if (!t) return -1;
    t->io_base = 0u;
    t->common_cfg = 0u;
    t->notify_cfg = 0u;
    t->notify_multiplier = 0u;
    t->device_cfg = 0u;
    t->modern = 0u;
    if (pci_find_device(VIRTIO_VENDOR, legacy_device, &t->pci) != 0 &&
        pci_find_device(VIRTIO_VENDOR, modern_device, &t->pci) != 0) return -1;
    (void)pci_enable_bus_master(&t->pci);
    if (find_caps(t)) {
        t->modern = 1u;
        return 0;
    }
    if ((t->pci.bar0 & 1u) == 0u) return -1;
    t->io_base = (u16)(t->pci.bar0 & 0xFFFCu);
    return t->io_base != 0u ? 0 : -1;
}

int virtio_pci_probe(virtio_pci_transport_t* t) {
    return virtio_pci_probe_device(t, VIRTIO_BLK_LEGACY, VIRTIO_BLK_MODERN);
}

void virtio_pci_reset(virtio_pci_transport_t* t) {
    if (!t) return;
    if (t->modern) mmio_write8(t->common_cfg + VIRTIO_COMMON_STATUS, 0u);
    else outb(t->io_base + VIRTIO_REG_STATUS, 0u);
}

void virtio_pci_set_status(virtio_pci_transport_t* t, u8 status) {
    if (!t) return;
    if (t->modern) mmio_write8(t->common_cfg + VIRTIO_COMMON_STATUS, status);
    else outb(t->io_base + VIRTIO_REG_STATUS, status);
}

u8 virtio_pci_get_status(const virtio_pci_transport_t* t) {
    if (!t) return 0u;
    return t->modern ? mmio_read8(t->common_cfg + VIRTIO_COMMON_STATUS)
                     : inb(t->io_base + VIRTIO_REG_STATUS);
}
