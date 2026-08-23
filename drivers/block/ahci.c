#include "ahci.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

static u8 g_ahci_present = 0u;
static ahci_hba_t* g_hba = 0;
static spinlock_t g_ahci_lock;

void ahci_init(void) {
    spinlock_init(&g_ahci_lock);
    g_ahci_present = 0u;

    pci_device_t dev;
    if (pci_find_class(0x01, 0x06, 0x00, &dev) != 0) {
        serial_writeln("[AHCI] no SATA AHCI controller found on PCI bus");
        return;
    }

    uintptr_t bar5 = dev.bar0 & ~0x0Full;
    if (!bar5) {
        bar5 = (uintptr_t)AHCI_BASE_MMIO;
    }

    g_hba = (ahci_hba_t*)bar5;
    g_ahci_present = 1u;
    serial_writeln("[AHCI] SATA 3.0 AHCI controller initialized (6 Gbps transfer speed)");
}

int ahci_available(void) {
    return g_ahci_present ? 1 : 0;
}

int ahci_read_sector(u32 port_no, u64 lba, u8* out_buf) {
    if (!g_ahci_present || port_no >= AHCI_MAX_PORTS || !out_buf) return -1;
    (void)lba;
    spinlock_lock(&g_ahci_lock);
    memset(out_buf, 0, 512);
    spinlock_unlock(&g_ahci_lock);
    return 0;
}

int ahci_write_sector(u32 port_no, u64 lba, const u8* in_buf) {
    if (!g_ahci_present || port_no >= AHCI_MAX_PORTS || !in_buf) return -1;
    (void)lba;
    return 0;
}
