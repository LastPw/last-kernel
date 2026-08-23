#include "virtio_blk.h"
#include "pci.h"
#include "io.h"
#include "string.h"
#include "serial.h"
#include "paging.h"
#include "interrupts.h"
#include "virtio_ring.h"
#include "virtio_pci.h"

#define VIRTIO_VENDOR 0x1AF4u
#define VIRTIO_BLK_LEGACY 0x1001u
#define VIRTIO_BLK_MODERN 0x1042u

#define VIRTIO_REG_DEVICE_FEATURES 0x00u
#define VIRTIO_REG_GUEST_FEATURES  0x04u
#define VIRTIO_REG_QUEUE_ADDRESS   0x08u
#define VIRTIO_REG_QUEUE_SIZE      0x0Cu
#define VIRTIO_REG_QUEUE_SELECT    0x0Eu
#define VIRTIO_REG_QUEUE_NOTIFY    0x10u
#define VIRTIO_REG_STATUS          0x12u
#define VIRTIO_REG_ISR             0x13u
#define VIRTIO_REG_CONFIG          0x14u

#define PCI_CAP_PTR                 0x34u
#define PCI_CAP_VENDOR              0x09u
#define VIRTIO_CAP_COMMON_CFG       1u
#define VIRTIO_CAP_NOTIFY_CFG       2u
#define VIRTIO_CAP_DEVICE_CFG       4u
#define VIRTIO_COMMON_FEATURE_SEL   0u
#define VIRTIO_COMMON_FEATURE       4u
#define VIRTIO_COMMON_GUEST_SEL     8u
#define VIRTIO_COMMON_GUEST_FEATURE 12u
#define VIRTIO_COMMON_STATUS        20u
#define VIRTIO_COMMON_QUEUE_SEL     22u
#define VIRTIO_COMMON_QUEUE_SIZE    24u
#define VIRTIO_COMMON_QUEUE_ENABLE  28u
#define VIRTIO_COMMON_QUEUE_NOTIFY  30u
#define VIRTIO_COMMON_QUEUE_DESC    32u
#define VIRTIO_COMMON_QUEUE_AVAIL   40u
#define VIRTIO_COMMON_QUEUE_USED    48u
#define VIRTIO_F_VERSION_1          32u

#define VIRTIO_STATUS_ACK          0x01u
#define VIRTIO_STATUS_DRIVER       0x02u
#define VIRTIO_STATUS_DRIVER_OK    0x04u
#define VIRTIO_STATUS_FEATURES_OK  0x08u
#define VIRTIO_STATUS_FAILED       0x80u

#define VIRTIO_BLK_T_IN            0u
#define VIRTIO_BLK_T_OUT           1u
#define VIRTIO_BLK_S_OK            0u

#define VIRTIO_QUEUE_SLOTS         256u
#define VIRTIO_QUEUE_BYTES         16384u

typedef struct __attribute__((packed)) {
    u32 type;
    u32 reserved;
    u64 sector;
} virtio_blk_header_t;

static u16 g_io;
static u8 g_modern;
static u32 g_common;
static u32 g_notify;
static u32 g_notify_multiplier;
static u32 g_device_config;
static u16 g_queue_size;
static u32 g_sectors;
static u8 g_ready;
static u8 g_status;
static u8 g_irq_line = 0xFFu;
static virtio_blk_header_t g_header __attribute__((aligned(16)));
static u8 g_data[512] __attribute__((aligned(16)));
static u8 g_request_status __attribute__((aligned(16)));
static u8 g_queue_memory[VIRTIO_QUEUE_BYTES] __attribute__((aligned(4096)));
static virtio_ring_t g_ring;
static virtio_pci_transport_t g_transport;

static void virtio_blk_irq(registers_t* regs) {
    (void)regs;
    /* Legacy VirtIO uses a read-to-clear ISR register.  Without consuming it,
       QEMU keeps the level-triggered PCI line asserted and starves the
       polling path before the used ring can be observed. */
    if (!g_modern && g_io != 0u) (void)inb(g_io + VIRTIO_REG_ISR);
}

static void virtio_serial_hex(u32 value) {
    static const char hex[] = "0123456789ABCDEF";
    for (i32 shift = 28; shift >= 0; shift -= 4) serial_putc(hex[(value >> (u32)shift) & 0x0Fu]);
}

static void virtio_status_set(u8 value) {
    g_status = value;
    outb(g_io + VIRTIO_REG_STATUS, value);
}

static int virtio_wait_used(void) {
    for (u32 retry = 0u; retry < 64u; retry++) {
        for (u32 spin = 0u; spin < 1000u; spin++) {
            if (virtio_ring_has_used(&g_ring)) return 0;
            __asm__ __volatile__("" ::: "memory");
        }
        /* A port I/O boundary gives emulated devices a chance to dispatch
           their deferred completion before the next ring check. */
        io_wait();
    }
    serial_write("[LastOS] virtio-blk timeout used=");
    serial_write_dec(*g_ring.used_idx);
    serial_write(" last=");
    serial_write_dec(g_ring.last_used);
    if (!g_modern) {
        serial_write(" isr=");
        serial_write_dec(inb(g_io + VIRTIO_REG_ISR));
    }
    serial_writeln("");
    return -1;
}

static int virtio_submit(u32 lba, u8 write) {
    virtio_ring_buffer_t buffers[3];
    u16 head;
    u16 old_avail_idx;
    u32 used_id;

    g_header.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    g_header.reserved = 0u;
    g_header.sector = (u64)lba;
    g_request_status = 0xFFu;

    buffers[0].addr_low = (u32)&g_header;
    buffers[0].addr_high = 0u;
    buffers[0].len = sizeof(g_header);
    buffers[0].flags = 0u;
    buffers[1].addr_low = (u32)g_data;
    buffers[1].addr_high = 0u;
    buffers[1].len = sizeof(g_data);
    buffers[1].flags = write ? 0u : VIRTIO_RING_DESC_WRITE;
    buffers[2].addr_low = (u32)&g_request_status;
    buffers[2].addr_high = 0u;
    buffers[2].len = 1u;
    buffers[2].flags = VIRTIO_RING_DESC_WRITE;
    old_avail_idx = *g_ring.avail_idx;
    interrupts_disable();
    if (virtio_ring_add_chain(&g_ring, buffers, 3u, &head) != 0 ||
        virtio_ring_publish(&g_ring, head) != 0) {
        interrupts_enable();
        return -1;
    }
    serial_writeln("[LastOS] virtio-blk submit published");
    /* Event Index is not negotiated, so every newly published request must
       notify the device.  Reading the optional event field could suppress
       the first request on a modern device. */
    (void)old_avail_idx;
    if (g_modern) {
        u16 notify_off = mmio_read16(g_common + VIRTIO_COMMON_QUEUE_NOTIFY);
        mmio_write16(g_notify + ((u32)notify_off * g_notify_multiplier), 0u);
    } else {
        outw(g_io + VIRTIO_REG_QUEUE_NOTIFY, 0u);
    }
    serial_writeln("[LastOS] virtio-blk submit notified");
    if (virtio_wait_used() != 0) {
        interrupts_enable();
        serial_writeln("[LastOS] virtio-blk queue completion failed; falling back");
        g_ready = 0u;
        return -1;
    }
    if (virtio_ring_get_used(&g_ring, &used_id, 0) != 0 || used_id != head) {
        interrupts_enable();
        serial_writeln("[LastOS] virtio-blk invalid used entry");
        return -1;
    }
    if (g_request_status != VIRTIO_BLK_S_OK) {
        interrupts_enable();
        serial_writeln("[LastOS] virtio-blk request error");
        return -1;
    }
    interrupts_enable();
    return 0;
}

int virtio_blk_init(void) {
    u32 capacity_low;
    u32 capacity_high;
    if (virtio_pci_probe(&g_transport) != 0) return -1;
    g_irq_line = (u8)pci_config_read32(g_transport.pci.bus, g_transport.pci.slot,
                                       g_transport.pci.func, 0x3Cu);
    serial_write("[LastOS] virtio-blk irq=");
    serial_write_dec(g_irq_line);
    serial_writeln("");
    if (g_irq_line < 16u) {
        irq_register_handler(g_irq_line, virtio_blk_irq);
        interrupts_unmask_irq(g_irq_line);
    }
    serial_writeln("[LastOS] virtio-blk PCI found");
    g_modern = g_transport.modern;
    g_io = g_transport.io_base;
    g_common = g_transport.common_cfg;
    g_notify = g_transport.notify_cfg;
    g_notify_multiplier = g_transport.notify_multiplier;
    g_device_config = g_transport.device_cfg;
    if (!g_modern) {
        if (!g_io) return -1;
    }

    if (g_modern) {
        u32 device_features_high;
        serial_write("[LastOS] virtio-blk modern common=0x"); virtio_serial_hex(g_common);
        serial_write(" notify=0x"); virtio_serial_hex(g_notify);
        serial_write(" config=0x"); virtio_serial_hex(g_device_config); serial_putc('\n');
        mmio_write8(g_common + VIRTIO_COMMON_STATUS, 0u);
        mmio_write8(g_common + VIRTIO_COMMON_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
        mmio_write32(g_common + VIRTIO_COMMON_FEATURE_SEL, 0u);
        (void)mmio_read32(g_common + VIRTIO_COMMON_FEATURE);
        mmio_write32(g_common + VIRTIO_COMMON_FEATURE_SEL, 1u);
        device_features_high = mmio_read32(g_common + VIRTIO_COMMON_FEATURE);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_SEL, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_FEATURE, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_SEL, 1u);
        mmio_write32(g_common + VIRTIO_COMMON_GUEST_FEATURE,
                     device_features_high & (1u << (VIRTIO_F_VERSION_1 - 32u)));
        mmio_write8(g_common + VIRTIO_COMMON_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
        if ((mmio_read8(g_common + VIRTIO_COMMON_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
            serial_writeln("[LastOS] virtio-blk feature negotiation failed");
            return -1;
        }
    } else {
        virtio_status_set(0u);
        virtio_status_set(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
        /* This first port intentionally accepts no optional features. */
        (void)inl(g_io + VIRTIO_REG_DEVICE_FEATURES);
        outl(g_io + VIRTIO_REG_GUEST_FEATURES, 0u);
        virtio_status_set(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
        if ((inb(g_io + VIRTIO_REG_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0u) {
            serial_writeln("[LastOS] virtio-blk feature negotiation failed");
            virtio_status_set(VIRTIO_STATUS_FAILED);
            return -1;
        }
    }
    if (g_modern) {
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_SEL, 0u);
        g_queue_size = mmio_read16(g_common + VIRTIO_COMMON_QUEUE_SIZE);
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_SIZE, g_queue_size);
    } else {
        outw(g_io + VIRTIO_REG_QUEUE_SELECT, 0u);
        g_queue_size = inw(g_io + VIRTIO_REG_QUEUE_SIZE);
    }
    if (g_queue_size == 0u || g_queue_size > VIRTIO_QUEUE_SLOTS) {
        serial_writeln("[LastOS] virtio-blk unsupported queue size");
        virtio_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }
    memset(g_queue_memory, 0, sizeof(g_queue_memory));
    if (virtio_ring_init(&g_ring, g_queue_memory, g_queue_size, 4096u) != 0) {
        serial_writeln("[LastOS] virtio-blk ring initialization failed");
        return -1;
    }
    if (g_modern) {
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_DESC, virtio_ring_desc_address(&g_ring));
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_DESC + 4u, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_AVAIL, virtio_ring_avail_address(&g_ring));
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_AVAIL + 4u, 0u);
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_USED, virtio_ring_used_address(&g_ring));
        mmio_write32(g_common + VIRTIO_COMMON_QUEUE_USED + 4u, 0u);
        mmio_write16(g_common + VIRTIO_COMMON_QUEUE_ENABLE, 1u);
        capacity_low = mmio_read32(g_device_config);
        capacity_high = mmio_read32(g_device_config + 4u);
    } else {
        outl(g_io + VIRTIO_REG_QUEUE_ADDRESS, (u32)g_queue_memory >> 12u);
        capacity_low = inl(g_io + VIRTIO_REG_CONFIG);
        capacity_high = inl(g_io + VIRTIO_REG_CONFIG + 4u);
    }
    g_sectors = capacity_low;
    if (capacity_high != 0u || g_sectors == 0u) {
        /* The current disk API is 32-bit LBA; reject devices it cannot address. */
        serial_writeln("[LastOS] virtio-blk invalid capacity");
        virtio_status_set(VIRTIO_STATUS_FAILED);
        return -1;
    }
    if (g_modern) {
        mmio_write8(g_common + VIRTIO_COMMON_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                    VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    } else {
        virtio_status_set(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK |
                          VIRTIO_STATUS_DRIVER_OK);
    }
    if ((g_modern ? mmio_read8(g_common + VIRTIO_COMMON_STATUS) : inb(g_io + VIRTIO_REG_STATUS)) & VIRTIO_STATUS_FAILED) {
        serial_writeln("[LastOS] virtio-blk device rejected features");
        return -1;
    }
    g_ready = 1u;
    serial_writeln("[LastOS] virtio-blk ready");
    return 0;
}

int virtio_blk_ready(void) {
    return g_ready ? 1 : 0;
}

u32 virtio_blk_sector_count(void) {
    return g_sectors;
}

int virtio_blk_read_sector(u32 lba, u8* out512) {
    if (!g_ready || !out512 || lba >= g_sectors) return -1;
    if (virtio_submit(lba, 0u) != 0) return -1;
    memcpy(out512, g_data, sizeof(g_data));
    return 0;
}

int virtio_blk_write_sector(u32 lba, const u8* in512) {
    if (!g_ready || !in512 || lba >= g_sectors) return -1;
    memcpy(g_data, in512, sizeof(g_data));
    return virtio_submit(lba, 1u);
}
