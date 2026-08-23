#include "nic.h"
#include "ethernet.h"
#include "ip.h"
#include "tcp_raw.h"
#include "udp_raw.h"
#include "checksum.h"
#include "netstack.h"
#include "threat_logger.h"
#include "io_scheduler.h"
#include "scheduler.h"
#include "sdc.h"
#include "pci.h"
#include "pcnet.h"
#include "virtio_net.h"
#include "io.h"
#include "string.h"

#define NIC_RX_RING 32
#define NIC_FRAME_MAX 1514
#define RTL8139_VENDOR 0x10ECu
#define RTL8139_DEVICE 0x8139u
#define RTL_RX_BYTES 8192u
#define RTL_RX_EXTRA 2048u
#define RTL_TX_SLOTS 4u

typedef struct {
    u16 len;
    u8 data[NIC_FRAME_MAX];
} nic_frame_t;

static nic_frame_t rx_ring[NIC_RX_RING];
static u32 rx_head = 0;
static u32 rx_tail = 0;
static u32 rx_count = 0;
static u16 rtl_io = 0u;
static u16 rtl_rx_offset = 0u;
static u8 rtl_ready = 0u;
static u8 rtl_mac[ETH_ADDR_LEN];
static u8 rtl_rx_buffer[RTL_RX_BYTES + RTL_RX_EXTRA] __attribute__((aligned(16)));
static u8 rtl_tx_buffer[RTL_TX_SLOTS][NIC_FRAME_MAX] __attribute__((aligned(4)));
static u8 rtl_tx_slot = 0u;
static u8 rtl_tx_inflight = 0u;

typedef enum {
    NIC_BACKEND_NONE = 0,
    NIC_BACKEND_RTL8139,
    NIC_BACKEND_PCNET
    ,NIC_BACKEND_VIRTIO_NET
} nic_backend_t;

static nic_backend_t nic_backend = NIC_BACKEND_NONE;

int nic_get_mac(u8* out_mac) {
    if (!out_mac) return -1;
    if (nic_backend == NIC_BACKEND_PCNET) return pcnet_get_mac(out_mac);
    if (nic_backend == NIC_BACKEND_VIRTIO_NET) return virtio_net_get_mac(out_mac);
    if (nic_backend != NIC_BACKEND_RTL8139 || !rtl_ready) return -1;
    for (u32 i = 0u; i < ETH_ADDR_LEN; i++) out_mac[i] = rtl_mac[i];
    return 0;
}

#define RTL_REG_IDR0 0x00u
#define RTL_REG_TSD0 0x10u
#define RTL_REG_TSAD0 0x20u
#define RTL_REG_RBSTART 0x30u
#define RTL_REG_CMD 0x37u
#define RTL_REG_CAPR 0x38u
#define RTL_REG_CBR 0x3Au
#define RTL_REG_IMR 0x3Cu
#define RTL_REG_ISR 0x3Eu
#define RTL_REG_TCR 0x40u
#define RTL_REG_RCR 0x44u
#define RTL_REG_CONFIG1 0x52u

#define RTL_CMD_RX_EMPTY 0x01u
#define RTL_CMD_TX_ENABLE 0x04u
#define RTL_CMD_RX_ENABLE 0x08u
#define RTL_CMD_RESET 0x10u
#define RTL_ISR_ROK 0x0001u
#define RTL_ISR_TOK 0x0004u
#define RTL_ISR_RX_ERROR 0x0002u
#define RTL_ISR_RX_OVERFLOW 0x0050u
#define RTL_RX_STATUS_OK 0x0001u
#define RTL_RX_STATUS_ERROR 0x003Eu
#define RTL_TX_STATUS_DONE 0x0000C000u
#define RTL_TX_STATUS_ABORTED 0x40000000u

static void rtl_delay(void) {
    for (volatile u32 i = 0u; i < 128u; i++) { }
}

static int rtl_find_device(pci_device_t* out) {
    if (pci_find_device(RTL8139_VENDOR, RTL8139_DEVICE, out) == 0) return 0;
    /* Linux's 8139 driver also accepts the compatible 8139C variant. */
    return pci_find_device(RTL8139_VENDOR, 0x8138u, out);
}

static int rtl8139_init(void) {
    pci_device_t dev;
    if (rtl_find_device(&dev) != 0) return -1;
    if ((dev.bar0 & 1u) == 0u) return -1; /* This driver currently uses the I/O BAR. */
    rtl_io = (u16)(dev.bar0 & 0xFFFCu);
    if (!rtl_io) return -1;
    (void)pci_enable_bus_master(&dev);

    outb(rtl_io + RTL_REG_CONFIG1, 0u);
    outb(rtl_io + RTL_REG_CMD, RTL_CMD_RESET);
    for (u32 i = 0u; i < 200u && (inb(rtl_io + RTL_REG_CMD) & RTL_CMD_RESET); i++) rtl_delay();
    if (inb(rtl_io + RTL_REG_CMD) & RTL_CMD_RESET) return -1;

    for (u32 i = 0u; i < ETH_ADDR_LEN; i++) rtl_mac[i] = inb(rtl_io + RTL_REG_IDR0 + (u16)i);
    memset(rtl_rx_buffer, 0, sizeof(rtl_rx_buffer));
    outl(rtl_io + RTL_REG_RBSTART, (u32)rtl_rx_buffer);
    outw(rtl_io + RTL_REG_CAPR, 0u);
    outw(rtl_io + RTL_REG_CBR, 0u);
    outw(rtl_io + RTL_REG_ISR, 0xFFFFu);
    outw(rtl_io + RTL_REG_IMR, RTL_ISR_ROK | RTL_ISR_TOK |
         RTL_ISR_RX_ERROR | RTL_ISR_RX_OVERFLOW);
    outl(rtl_io + RTL_REG_RCR, 0x0000000Fu | (1u << 7) | (1u << 3));
    outl(rtl_io + RTL_REG_TCR, 0x03000700u);
    outb(rtl_io + RTL_REG_CMD, RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE);
    rtl_rx_offset = 0u;
    rtl_tx_slot = 0u;
    rtl_tx_inflight = 0u;
    rtl_ready = 1u;
    return 0;
}

static int queue_rx(const u8* frame, u16 len) {
    if (!frame || len < sizeof(eth_header_t) || len > NIC_FRAME_MAX) {
        return -1;
    }
    if (rx_count >= NIC_RX_RING) {
        threat_logger_log(THREAT_IO_MALFORMED, 0, 201, len);
        return -1;
    }

    nic_frame_t* slot = &rx_ring[rx_tail];
    slot->len = len;
    for (u16 i = 0; i < len; i++) {
        slot->data[i] = frame[i];
    }

    rx_tail = (rx_tail + 1) % NIC_RX_RING;
    rx_count++;
    return 0;
}

static void rtl_poll_receive(void) {
    if (!rtl_ready) return;
    while ((inb(rtl_io + RTL_REG_CMD) & RTL_CMD_RX_EMPTY) == 0u) {
        u8* packet = rtl_rx_buffer + rtl_rx_offset;
        u16 status = (u16)packet[0] | ((u16)packet[1] << 8);
        u16 len = (u16)packet[2] | ((u16)packet[3] << 8);
        /* A zero length means the controller is still filling the header. */
        if (len == 0u || len == 0xFFF0u) break;
        if (len < 4u || len > NIC_FRAME_MAX + 4u) {
            threat_logger_log(THREAT_IO_MALFORMED, 0, 202, len);
            break;
        }
        if ((status & RTL_RX_STATUS_OK) == 0u || (status & RTL_RX_STATUS_ERROR) != 0u) {
            threat_logger_log(THREAT_IO_MALFORMED, 0, 203, status);
        } else if (rtl_rx_offset + 4u + len <= RTL_RX_BYTES + RTL_RX_EXTRA) {
            /* The extra tail mirrors Linux's RX_BUF_WRAP_PAD policy. */
            (void)queue_rx(packet + 4u, (u16)(len - 4u));
        }
        rtl_rx_offset = (u16)((rtl_rx_offset + len + 4u + 3u) & ~3u);
        if (rtl_rx_offset >= RTL_RX_BYTES) rtl_rx_offset = (u16)(rtl_rx_offset - RTL_RX_BYTES);
        outw(rtl_io + RTL_REG_CAPR, (u16)((rtl_rx_offset + RTL_RX_BYTES - 16u) % RTL_RX_BYTES));
    }
}

void nic_init(void) {
    rx_head = 0;
    rx_tail = 0;
    rx_count = 0;
    rtl_ready = 0u;
    rtl_io = 0u;
    nic_backend = NIC_BACKEND_NONE;
    /* Prefer paravirtual hardware when present.  RTL8139/PCnet remain real
       fallbacks for VirtualBox profiles and older machines. */
    if (virtio_net_init() == 0) {
        nic_backend = NIC_BACKEND_VIRTIO_NET;
        (void)virtio_net_get_mac(rtl_mac);
        ethernet_set_local_mac(rtl_mac);
    } else if (pcnet_init() == 0) {
        nic_backend = NIC_BACKEND_PCNET;
        (void)pcnet_get_mac(rtl_mac);
        ethernet_set_local_mac(rtl_mac);
    } else if (rtl8139_init() == 0) {
        nic_backend = NIC_BACKEND_RTL8139;
        ethernet_set_local_mac(rtl_mac);
    }
}

int nic_ready(void) {
    return nic_backend != NIC_BACKEND_NONE ? 1 : 0;
}

const char* nic_backend_name(void) {
    if (nic_backend == NIC_BACKEND_RTL8139) return "RTL8139";
    if (nic_backend == NIC_BACKEND_PCNET) return "PCnet/VirtualBox";
    if (nic_backend == NIC_BACKEND_VIRTIO_NET) return "VirtIO-net";
    return "none";
}

int nic_send_frame(const u8* frame, u16 len) {
    if (!frame || len < sizeof(eth_header_t) || len > NIC_FRAME_MAX) {
        return -1;
    }

    if (!nic_ready()) return -1;
    io_scheduler_submit(scheduler_current_task(), len, 0);
    io_scheduler_account_net_packet();
    sdc_on_io(scheduler_current_task(), len);
    if (len > 1500) {
        sdc_on_packet(len, 1);
    }
    if (nic_backend == NIC_BACKEND_PCNET) return pcnet_send(frame, len);
    if (nic_backend == NIC_BACKEND_VIRTIO_NET) return virtio_net_send(frame, len);

    u8 slot = rtl_tx_slot;
    if (rtl_tx_inflight & (u8)(1u << slot)) {
        u32 status = inl(rtl_io + RTL_REG_TSD0 + ((u16)slot * 4u));
        if ((status & (RTL_TX_STATUS_DONE | RTL_TX_STATUS_ABORTED)) == 0u) return -2;
        rtl_tx_inflight &= (u8)~(1u << slot);
    }
    memcpy(rtl_tx_buffer[slot], frame, len);
    outl(rtl_io + RTL_REG_TSAD0 + ((u16)slot * 4u), (u32)rtl_tx_buffer[slot]);
    outl(rtl_io + RTL_REG_TSD0 + ((u16)slot * 4u), len);
    rtl_tx_inflight |= (u8)(1u << slot);
    rtl_tx_slot = (u8)((slot + 1u) % RTL_TX_SLOTS);
    return 0;
}

u32 nic_poll(u32 budget) {
    u32 handled = 0;
    if (nic_backend == NIC_BACKEND_PCNET) {
        (void)pcnet_poll(budget, queue_rx);
    } else if (nic_backend == NIC_BACKEND_VIRTIO_NET) {
        (void)virtio_net_poll(budget, queue_rx);
    } else if (nic_backend == NIC_BACKEND_RTL8139) {
        rtl_poll_receive();
    }
    while (rx_count > 0 && handled < budget) {
        nic_frame_t* slot = &rx_ring[rx_head];
        netstack_on_frame(slot->data, slot->len);
        io_scheduler_account_net_packet();
        sdc_on_packet(slot->len, 0);
        rx_head = (rx_head + 1) % NIC_RX_RING;
        rx_count--;
        handled++;
    }
    return handled;
}
