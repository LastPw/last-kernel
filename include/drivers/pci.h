#ifndef PCI_H
#define PCI_H

#include "types.h"

typedef struct {
    u8 bus;
    u8 slot;
    u8 func;
    u16 vendor;
    u16 device;
    u8 irq;
    u32 bar0;
} pci_device_t;

u32 pci_config_read32(u8 bus, u8 slot, u8 func, u8 offset);
void pci_config_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
int pci_find_device(u16 vendor, u16 device, pci_device_t* out);
int pci_find_class(u8 class_code, u8 subclass, u8 prog_if, pci_device_t* out);
int pci_enable_bus_master(const pci_device_t* dev);

/* PCIe MMCONFIG (memory-mapped PCI config space) */
int  pcie_mmconfig_init(void);
int  pcie_available(void);
u32  pcie_config_read32(u8 bus, u8 slot, u8 func, u8 offset);
void pcie_config_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
u32  pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);

#endif
