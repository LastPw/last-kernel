#include "ac97.h"
#include "pci.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"
#include "string.h"
#include "frame_allocator.h"
#include "paging.h"
#include "timer.h"

#define AC97_NAM_MASTER_VOL    0x02u
#define AC97_NAM_PCM_VOL       0x18u
#define AC97_NAM_RESET         0x00u

static u8 g_ac97_present = 0u;
static u16 g_nam_base = 0u;
static u16 g_nabm_base = 0u;
static spinlock_t g_ac97_lock;

/* ── PCM output (NABM PO bus-master block) ── */
#define AC97_SRATE_DEFAULT 48000u
#define AC97_PO_BASE       0x10u   /* PO block inside NABM */
#define AC97_PO_BDBAR      (AC97_PO_BASE + 0x00u)
#define AC97_PO_LVI        (AC97_PO_BASE + 0x05u)
#define AC97_PO_SR         (AC97_PO_BASE + 0x06u)
#define AC97_PO_CR         (AC97_PO_BASE + 0x09u)
#define AC97_GLOB_CNT      0x2Cu
#define AC97_GLOB_STA      0x30u
#define AC97_NAM_DAC_RATE  0x2Cu

#define AC97_CR_RPBM       0x01u
#define AC97_CR_RR         0x02u
#define AC97_CR_IOCE       0x10u
#define AC97_SR_DCH        0x0001u /* DMA halted (ro) */
#define AC97_SR_WCLEAR     0x001Cu /* LVBCI|BCIS|FIFOE (w1c) */

#define AC97_BDL_ENTRIES   2u
#define AC97_BUF_SAMPLES   16384u  /* per descriptor, 16-bit units */

typedef struct { u32 addr; u32 ctl_len; } ac97_bdl_entry_t;

static u32 g_bdl_pa = 0u;
static ac97_bdl_entry_t* g_bdl = 0;
static void* g_buf[AC97_BDL_ENTRIES];
static u32 g_buf_pa[AC97_BDL_ENTRIES];
static u32 g_rate = AC97_SRATE_DEFAULT;

static inline u32 codec_read16(u8 reg) {
    outw(g_nam_base + reg, 0u);
    return inw(g_nam_base + reg);
}
static inline int codec_write16(u8 reg, u16 val) {
    for (u32 i = 0; i < 100u; i++) {
        if ((inw(g_nam_base + AC97_NAM_RESET) & 0x8000u) == 0u) break;
    }
    outw(g_nam_base + reg, val);
    return 0;
}

void ac97_init(void) {
    if (g_ac97_present) return; /* idempotent: bus scans call this repeatedly */
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

    if (g_nabm_base != 0u) {
        /* Controller cold reset, then bring the AC-link up. */
        outw(g_nabm_base + AC97_GLOB_CNT, 0x0002u);
        for (volatile u32 i = 0; i < 100000u; i++) { }
        outw(g_nabm_base + AC97_GLOB_CNT, 0x0001u);

        /* Halt any output stream from a previous life. */
        outb(g_nabm_base + AC97_PO_CR, AC97_CR_RR);
    }
    if (g_nam_base != 0u) {
        /* Reset the codec register set */
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


/* Copy PCM into the DMA ring and run the PO bus-master engine.
 * pcm is 16-bit little-endian; channels 1 or 2 interleaved. */
int ac97_play_pcm(const void* pcm_data, u32 len_bytes, u32 sample_rate, u8 channels) {
    if (!g_ac97_present || !g_nabm_base || !pcm_data || len_bytes == 0u) return -1;
    if (channels != 1u && channels != 2u) return -1;
    if (len_bytes > AC97_BDL_ENTRIES * AC97_BUF_SAMPLES * 2u) return -2;

    spinlock_lock(&g_ac97_lock);

    /* one-time DMA buffer allocation */
    if (!g_bdl) {
        for (u32 i = 0; i < AC97_BDL_ENTRIES; i++) {
            void* b = frame_alloc_dma32();
            if (!b) { spinlock_unlock(&g_ac97_lock); return -3; }
            g_buf[i] = (void*)paging_phys_to_virt((u64)(uintptr_t)b);
            g_buf_pa[i] = (u32)(uintptr_t)b;
        }
        void* bd = frame_alloc_dma32();
        if (!bd) { spinlock_unlock(&g_ac97_lock); return -3; }
        g_bdl_pa = (u32)(uintptr_t)bd;
        g_bdl = (ac97_bdl_entry_t*)paging_phys_to_virt((u64)(uintptr_t)bd);
        serial_writeln("[AC97] DMA ring allocated (dma32)");
    }

    /* DAC sample rate */
    if (sample_rate >= 8000u && sample_rate <= 48000u && sample_rate != g_rate) {
        codec_write16(AC97_NAM_DAC_RATE, (u16)(sample_rate & 0xFFFFu));
        g_rate = sample_rate;
    }

    /* stage into the two descriptors */
    u32 half = len_bytes / AC97_BDL_ENTRIES;
    u32 off = 0u;
    for (u32 i = 0; i < AC97_BDL_ENTRIES; i++) {
        u32 chunk = half;
        if (off + chunk > len_bytes || i == AC97_BDL_ENTRIES - 1u) {
            chunk = len_bytes - off;
        }
        memcpy(g_buf[i], (const u8*)pcm_data + off, chunk);
        off += chunk;

        /* ctl_len low16 = sample units (16-bit); pad tail to keep DMA flowing */
        u16 samples = (u16)(chunk / 2u);
        g_bdl[i].addr = g_buf_pa[i];
        g_bdl[i].ctl_len = (u32)samples; /* last entry: no IOC bit, halts after */
    }
    /* fixup: first entry carries IOC so we get an interrupt-free progress
       marker via CIV; final halt detection uses SR.DCH */

    /* program and start the output engine */
    outl(g_nabm_base + AC97_PO_BDBAR, g_bdl_pa);
    outb(g_nabm_base + AC97_PO_CR, AC97_CR_RR);          /* reset descriptor ptr */
    outb(g_nabm_base + AC97_PO_LVI, (u8)(AC97_BDL_ENTRIES - 1u));
    outb(g_nabm_base + AC97_PO_CR, AC97_CR_RPBM | AC97_CR_IOCE);
    serial_write("[AC97] started bdbar_rb=");
    serial_write_hex(inl(g_nabm_base + AC97_PO_BDBAR));
    serial_write(" lvi_rb=");
    serial_write_dec(inb(g_nabm_base + AC97_PO_LVI));
    serial_write(" cr_rb=");
    serial_write_hex(inb(g_nabm_base + AC97_PO_CR));
    serial_write(" gsta=");
    serial_write_hex(inw(g_nabm_base + AC97_GLOB_STA));
    serial_putc('\n');

    /* wait for transfer end: controller halts (SR.DCH) after LVI w/o reload */
    u32 guard = 40u * (timer_frequency() ? timer_frequency() : 1000u);
    while (guard--) {
        u16 sr = inw(g_nabm_base + AC97_PO_SR);
        if (sr & AC97_SR_DCH) break;
        if ((sr & 0x01u) == 0u && (inw(g_nabm_base + AC97_PO_SR) & AC97_SR_DCH)) break;
    }
    serial_write("[AC97] done civ=");
    serial_write_dec(inb(g_nabm_base + AC97_PO_BASE + 4u));
    serial_write(" sr=");
    serial_write_hex(inw(g_nabm_base + AC97_PO_SR));
    serial_write(" picb=");
    serial_write_dec(inw(g_nabm_base + AC97_PO_BASE + 8u));
    serial_putc('\n');
    outb(g_nabm_base + AC97_PO_CR, 0u);

    spinlock_unlock(&g_ac97_lock);
    return 0;
}

int ac97_beep(u16 freq_hz, u32 ms) {
    static u8 tone[AC97_BUF_SAMPLES * 2u * AC97_BDL_ENTRIES];
    if (!freq_hz) freq_hz = 440u;
    if (ms < 10u) ms = 10u;
    if (ms > 1500u) ms = 1500u;

    u32 rate = AC97_SRATE_DEFAULT;
    u32 total_samples = (rate * ms) / 1000u;
    if (total_samples > AC97_BUF_SAMPLES * AC97_BDL_ENTRIES)
        total_samples = AC97_BUF_SAMPLES * AC97_BDL_ENTRIES;

    for (u32 i = 0; i < total_samples; i++) {
        /* cheap square wave -- audible and unambiguous */
        u8 v = ((i * freq_hz) / rate) & 1u ? 0x50u : 0xB0u;
        tone[i * 2u + 0u] = v;
        tone[i * 2u + 1u] = v;
    }
    return ac97_play_pcm(tone, total_samples * 2u, rate, 1u);
}

u32 ac97_sample_rate(void) { return g_rate; }

