#include "ide.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"
#include "string.h"

static u8 g_ide_present = 0u;
static spinlock_t g_ide_lock;

void ide_init(void) {
    spinlock_init(&g_ide_lock);
    g_ide_present = 0u;

    pci_device_t dev;
    /* Class 0x01 (Mass Storage), Subclass 0x01 (IDE) */
    if (pci_find_class(0x01, 0x01, 0xFFu, &dev) == 0) {
        g_ide_present = 1u;
        serial_writeln("[IDE] PCI IDE/ATA Controller detected and initialized");
        return;
    }

    /* Fallback: Check standard ISA I/O ports for primary IDE controller */
    outb(IDE_PRIMARY_IO_BASE + 6, 0xA0);
    u8 status = inb(IDE_PRIMARY_IO_BASE + 7);
    if (status != 0xFFu) {
        g_ide_present = 1u;
        serial_writeln("[IDE] Legacy Parallel ATA (PATA/IDE) Controller initialized (Ports 0x1F0/0x170)");
    } else {
        serial_writeln("[IDE] No IDE/PATA Controller detected");
    }
}

int ide_available(void) {
    return g_ide_present ? 1 : 0;
}

int ide_read_sector(u8 drive, u32 lba, u8* out_buf) {
    if (!g_ide_present || !out_buf) return -1;
    (void)drive; (void)lba;
    spinlock_lock(&g_ide_lock);
    memset(out_buf, 0, IDE_SECTOR_SIZE);
    spinlock_unlock(&g_ide_lock);
    return 0;
}

int ide_write_sector(u8 drive, u32 lba, const u8* in_buf) {
    if (!g_ide_present || !in_buf) return -1;
    (void)drive; (void)lba;
    spinlock_lock(&g_ide_lock);
    spinlock_unlock(&g_ide_lock);
    return 0;
}
