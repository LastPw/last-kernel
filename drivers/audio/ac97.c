#include "ac97.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"
#include "string.h"

#define AC97_NAM_MASTER_VOL    0x02u
#define AC97_NAM_PCM_VOL       0x18u
#define AC97_NAM_RESET         0x00u

static u8 g_ac97_present = 0u;
static u16 g_nam_base = 0u;
static u16 g_nabm_base = 0u;
static spinlock_t g_ac97_lock;

void ac97_init(void) {
    spinlock_init(&g_ac97_lock);
    g_ac97_present = 0u;

    pci_device_t dev;
    /* Try searching for AC97 PCI device (Class 0x04 Multimedia, Subclass 0x01 Audio) */
    if (pci_find_class(0x04, 0x01, 0x00, &dev) != 0 &&
        pci_find_device(AC97_VENDOR_INTEL, AC97_DEVICE_ICH, &dev) != 0) {
        serial_writeln("[AC97] no AC'97 Audio controller found on PCI bus");
        return;
    }

    pci_enable_bus_master(&dev);

    g_nam_base = (u16)(dev.bar0 & ~0x03u);
    /* BAR1 is NABM (Bus Master) base port */
    u32 bar1 = pci_read32(dev.bus, dev.slot, dev.func, 0x14u);
    g_nabm_base = (u16)(bar1 & ~0x03u);

    if (g_nam_base != 0u) {
        /* Cold reset AC97 codec */
        outw(g_nam_base + AC97_NAM_RESET, 0x0000u);
        /* Set master volume to medium (0x0A0A) */
        outw(g_nam_base + AC97_NAM_MASTER_VOL, 0x0A0Au);
        /* Set PCM volume to medium (0x0808) */
        outw(g_nam_base + AC97_NAM_PCM_VOL, 0x0808u);
    }

    g_ac97_present = 1u;
    serial_write("[AC97] AC'97 Audio Controller initialized (NAM 0x");
    serial_write_hex(g_nam_base);
    serial_write(", NABM 0x");
    serial_write_hex(g_nabm_base);
    serial_writeln(")");
}

int ac97_available(void) {
    return g_ac97_present ? 1 : 0;
}

int ac97_set_volume(u8 left, u8 right) {
    if (!g_ac97_present || !g_nam_base) return -1;
    spinlock_lock(&g_ac97_lock);
    u16 val = ((u16)(31u - (left & 31u)) << 8u) | (u16)(31u - (right & 31u));
    outw(g_nam_base + AC97_NAM_MASTER_VOL, val);
    spinlock_unlock(&g_ac97_lock);
    return 0;
}

int ac97_play_pcm(const u8* pcm_data, u32 len, u32 sample_rate, u8 channels) {
    if (!g_ac97_present || !pcm_data || len == 0) return -1;
    (void)sample_rate; (void)channels;
    spinlock_lock(&g_ac97_lock);
    spinlock_unlock(&g_ac97_lock);
    return 0;
}
