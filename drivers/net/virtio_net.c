#include "virtio_net.h"
#include "virtio_ring.h"
#include "virtio_pci.h"
#include "pci.h"
#include "interrupts.h"
#include "io.h"
#include "serial.h"
#include "string.h"

#define VIRTIO_VENDOR 0x1AF4u
#define VIRTIO_NET_DEVICE 0x1000u
#define VIRTIO_NET_MODERN 0x1041u
#define VNET_STATUS_ACK 0x01u
#define VNET_STATUS_DRIVER 0x02u
#define VNET_STATUS_FEATURES_OK 0x08u
#define VNET_STATUS_DRIVER_OK 0x04u
#define VNET_STATUS_FAILED 0x80u
#define VNET_REG_DEVICE_FEATURES 0x00u
#define VNET_REG_GUEST_FEATURES 0x04u
#define VNET_REG_QUEUE_ADDRESS 0x08u
#define VNET_REG_QUEUE_SIZE 0x0Cu
#define VNET_REG_QUEUE_SELECT 0x0Eu
#define VNET_REG_QUEUE_NOTIFY 0x10u
#define VNET_REG_STATUS 0x12u
#define VNET_REG_CONFIG 0x14u
#define VNET_QUEUE_SIZE 8u
#define VNET_FRAME_MAX 1514u
#define VNET_HDR_SIZE 10u
#define VNET_COMMON_DEVICE_FEATURE_SELECT 0u
#define VNET_COMMON_DEVICE_FEATURE 4u
#define VNET_COMMON_DRIVER_FEATURE_SELECT 8u
#define VNET_COMMON_DRIVER_FEATURE 12u
#define VNET_COMMON_STATUS 20u
#define VNET_COMMON_QUEUE_SELECT 22u
#define VNET_COMMON_QUEUE_SIZE 24u
#define VNET_COMMON_QUEUE_ENABLE 28u
#define VNET_COMMON_QUEUE_DESC 32u
#define VNET_COMMON_QUEUE_AVAIL 40u
#define VNET_COMMON_QUEUE_USED 48u

static pci_device_t g_dev;
static u16 g_io;
static u8 g_ready;
static u8 g_modern;
static u8 g_mac[6];
static virtio_pci_transport_t g_transport;
static u8 g_rx_storage[VNET_QUEUE_SIZE][VNET_HDR_SIZE + VNET_FRAME_MAX] __attribute__((aligned(16)));
static u8 g_tx_storage[VNET_HDR_SIZE + VNET_FRAME_MAX] __attribute__((aligned(16)));
static u8 g_rx_ring_mem[4096] __attribute__((aligned(4096)));
static u8 g_tx_ring_mem[4096] __attribute__((aligned(4096)));
static virtio_ring_t g_rx_ring;
static virtio_ring_t g_tx_ring;

static int vnet_queue_init(u16 index, virtio_ring_t* ring, void* memory) {
    u16 size;
    if (g_modern) {
        mmio_write16(g_transport.common_cfg + VNET_COMMON_QUEUE_SELECT, index);
        size = mmio_read16(g_transport.common_cfg + VNET_COMMON_QUEUE_SIZE);
        if (size < VNET_QUEUE_SIZE) return -1;
        mmio_write16(g_transport.common_cfg + VNET_COMMON_QUEUE_SIZE, VNET_QUEUE_SIZE);
        if (virtio_ring_init(ring, memory, VNET_QUEUE_SIZE, 4096u) != 0) return -1;
        mmio_write32(g_transport.common_cfg + VNET_COMMON_QUEUE_DESC, virtio_ring_desc_address(ring));
        mmio_write32(g_transport.common_cfg + VNET_COMMON_QUEUE_DESC + 4u, 0u);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_QUEUE_AVAIL, virtio_ring_avail_address(ring));
        mmio_write32(g_transport.common_cfg + VNET_COMMON_QUEUE_AVAIL + 4u, 0u);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_QUEUE_USED, virtio_ring_used_address(ring));
        mmio_write32(g_transport.common_cfg + VNET_COMMON_QUEUE_USED + 4u, 0u);
        mmio_write16(g_transport.common_cfg + VNET_COMMON_QUEUE_ENABLE, 1u);
        return 0;
    }
    outw(g_io + VNET_REG_QUEUE_SELECT, index);
    if (inw(g_io + VNET_REG_QUEUE_SIZE) < VNET_QUEUE_SIZE) return -1;
    outw(g_io + VNET_REG_QUEUE_SIZE, VNET_QUEUE_SIZE);
    if (virtio_ring_init(ring, memory, VNET_QUEUE_SIZE, 4096u) != 0) return -1;
    outl(g_io + VNET_REG_QUEUE_ADDRESS, (u32)memory >> 12u);
    return 0;
}

static void vnet_notify(u16 queue) {
    if (g_modern) {
        u16 notify_off;
        mmio_write16(g_transport.common_cfg + VNET_COMMON_QUEUE_SELECT, queue);
        notify_off = mmio_read16(g_transport.common_cfg + 30u);
        mmio_write16(g_transport.notify_cfg + ((u32)notify_off * g_transport.notify_multiplier), 0u);
    } else {
        outw(g_io + VNET_REG_QUEUE_NOTIFY, queue);
    }
}

static int vnet_post_rx(u32 slot) {
    virtio_ring_buffer_t b;
    u16 head;
    b.addr_low = (u32)g_rx_storage[slot]; b.addr_high = 0u;
    b.len = sizeof(g_rx_storage[slot]);
    b.flags = VIRTIO_RING_DESC_WRITE;
    if (virtio_ring_add_chain(&g_rx_ring, &b, 1u, &head) != 0) return -1;
    if (virtio_ring_publish(&g_rx_ring, head) != 0) return -1;
    return 0;
}

int virtio_net_init(void) {
    g_ready = 0u;
    if (virtio_pci_probe_device(&g_transport, VIRTIO_NET_DEVICE, VIRTIO_NET_MODERN) != 0) return -1;
    g_modern = g_transport.modern;
    g_dev = g_transport.pci;
    g_io = g_transport.io_base;
    if (pci_enable_bus_master(&g_dev) != 0) return -1;
    if (g_modern) {
        mmio_write8(g_transport.common_cfg + VNET_COMMON_STATUS, 0u);
        mmio_write8(g_transport.common_cfg + VNET_COMMON_STATUS, VNET_STATUS_ACK | VNET_STATUS_DRIVER);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_DEVICE_FEATURE_SELECT, 0u);
        (void)mmio_read32(g_transport.common_cfg + VNET_COMMON_DEVICE_FEATURE);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_DEVICE_FEATURE_SELECT, 1u);
        (void)mmio_read32(g_transport.common_cfg + VNET_COMMON_DEVICE_FEATURE);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_DRIVER_FEATURE_SELECT, 0u);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_DRIVER_FEATURE, 0u);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_DRIVER_FEATURE_SELECT, 1u);
        mmio_write32(g_transport.common_cfg + VNET_COMMON_DRIVER_FEATURE, 0u);
        mmio_write8(g_transport.common_cfg + VNET_COMMON_STATUS, VNET_STATUS_ACK | VNET_STATUS_DRIVER | VNET_STATUS_FEATURES_OK);
        if ((mmio_read8(g_transport.common_cfg + VNET_COMMON_STATUS) & VNET_STATUS_FEATURES_OK) == 0u) return -1;
        for (u32 i = 0u; i < 6u; i++) g_mac[i] = mmio_read8(g_transport.device_cfg + i);
    } else {
        for (u32 i = 0u; i < 6u; i++) g_mac[i] = inb(g_io + VNET_REG_CONFIG + (u16)i);
        outb(g_io + VNET_REG_STATUS, 0u);
        outb(g_io + VNET_REG_STATUS, VNET_STATUS_ACK | VNET_STATUS_DRIVER);
        (void)inl(g_io + VNET_REG_DEVICE_FEATURES);
        outl(g_io + VNET_REG_GUEST_FEATURES, 0u);
        outb(g_io + VNET_REG_STATUS, VNET_STATUS_ACK | VNET_STATUS_DRIVER | VNET_STATUS_FEATURES_OK);
        if ((inb(g_io + VNET_REG_STATUS) & VNET_STATUS_FEATURES_OK) == 0u) return -1;
    }
    if (vnet_queue_init(0u, &g_rx_ring, g_rx_ring_mem) != 0 || vnet_queue_init(1u, &g_tx_ring, g_tx_ring_mem) != 0) return -1;
    memset(g_rx_storage, 0, sizeof(g_rx_storage));
    memset(g_tx_storage, 0, sizeof(g_tx_storage));
    for (u32 i = 0u; i < VNET_QUEUE_SIZE; i++) if (vnet_post_rx(i) != 0) return -1;
    vnet_notify(0u);
    if (g_modern) {
        mmio_write8(g_transport.common_cfg + VNET_COMMON_STATUS, VNET_STATUS_ACK | VNET_STATUS_DRIVER |
                    VNET_STATUS_FEATURES_OK | VNET_STATUS_DRIVER_OK);
        if (mmio_read8(g_transport.common_cfg + VNET_COMMON_STATUS) & VNET_STATUS_FAILED) return -1;
    } else {
        outb(g_io + VNET_REG_STATUS, VNET_STATUS_ACK | VNET_STATUS_DRIVER | VNET_STATUS_FEATURES_OK | VNET_STATUS_DRIVER_OK);
        if (inb(g_io + VNET_REG_STATUS) & VNET_STATUS_FAILED) return -1;
    }
    g_ready = 1u;
    serial_write("[LastOS] virtio-net ");
    serial_writeln(g_modern ? "modern ready" : "legacy ready");
    return 0;
}

int virtio_net_ready(void) { return g_ready ? 1 : 0; }

int virtio_net_get_mac(u8* out_mac) {
    if (!g_ready || !out_mac) return -1;
    memcpy(out_mac, g_mac, 6u); return 0;
}

int virtio_net_send(const u8* frame, u16 len) {
    virtio_ring_buffer_t b[2]; u16 head; u32 used_id;
    if (!g_ready || !frame || len < 14u || len > VNET_FRAME_MAX) return -1;
    memset(g_tx_storage, 0, sizeof(g_tx_storage));
    memcpy(g_tx_storage + VNET_HDR_SIZE, frame, len);
    b[0].addr_low = (u32)g_tx_storage; b[0].addr_high = 0u; b[0].len = VNET_HDR_SIZE; b[0].flags = 0u;
    b[1].addr_low = (u32)(g_tx_storage + VNET_HDR_SIZE); b[1].addr_high = 0u; b[1].len = len; b[1].flags = 0u;
    if (virtio_ring_add_chain(&g_tx_ring, b, 2u, &head) != 0 || virtio_ring_publish(&g_tx_ring, head) != 0) return -1;
    vnet_notify(1u);
    for (u32 spin = 0u; spin < 1000000u; spin++) if (virtio_ring_has_used(&g_tx_ring)) {
        if (virtio_ring_get_used(&g_tx_ring, &used_id, 0) != 0 || used_id != head) return -1;
        return 0;
    }
    return -1;
}

u32 virtio_net_poll(u32 budget, virtio_net_rx_handler_t handler) {
    u32 done = 0u, id, length;
    if (!g_ready || !handler) return 0u;
    while (done < budget && virtio_ring_has_used(&g_rx_ring)) {
        if (virtio_ring_get_used(&g_rx_ring, &id, &length) != 0 || id >= VNET_QUEUE_SIZE) break;
        if (length > VNET_HDR_SIZE && length <= sizeof(g_rx_storage[id])) {
            u16 frame_len = (u16)(length - VNET_HDR_SIZE);
            (void)handler(g_rx_storage[id] + VNET_HDR_SIZE, frame_len);
        }
        (void)vnet_post_rx(id);
        done++;
    }
    if (done) vnet_notify(0u);
    return done;
}
