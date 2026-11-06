#include "pcnet.h"
#include "pci.h"
#include "io.h"
#include "string.h"

#define PCNET_VENDOR 0x1022u
#define PCNET_DEVICE 0x2000u
#define PCNET_IO_RDP 0x10u
#define PCNET_IO_RAP 0x12u
#define PCNET_IO_RESET 0x14u
#define PCNET_IO_BDP 0x16u

#define PCNET_CSR0_INIT 0x0001u
#define PCNET_CSR0_START 0x0002u
#define PCNET_CSR0_STOP 0x0004u
#define PCNET_CSR0_TDMD 0x0008u
#define PCNET_CSR0_IDON 0x0100u
#define PCNET_CSR0_TINT 0x0200u
#define PCNET_CSR0_RINT 0x0400u
#define PCNET_CSR0_IENA 0x0040u

#define PCNET_DESC_OWN 0x8000u
#define PCNET_DESC_ERR 0x4000u
#define PCNET_DESC_STP 0x0200u
#define PCNET_DESC_ENP 0x0100u
#define PCNET_RING_SIZE 16u
#define PCNET_BUFFER_SIZE 1536u
#define PCNET_RING_LOG2 4u

typedef struct __attribute__((packed, aligned(16))) {
    u32 base;
    u16 buf_len;
    u16 status;
    u32 msg_len;
    u32 reserved;
} pcnet_desc_t;

typedef struct __attribute__((packed, aligned(16))) {
    u16 mode;
    u8 rlen;
    u8 tlen;
    u8 mac[6];
    u16 reserved;
    u32 filter0;
    u32 filter1;
    u32 rx_ring;
    u32 tx_ring;
} pcnet_init_block_t;

static u16 g_io;
static u8 g_ready;
static u8 g_mac[6];
static u32 g_rx_index;
static u32 g_tx_index;
static pcnet_init_block_t g_init __attribute__((aligned(16)));
static pcnet_desc_t g_rx[PCNET_RING_SIZE] __attribute__((aligned(16)));
static pcnet_desc_t g_tx[PCNET_RING_SIZE] __attribute__((aligned(16)));
static u8 g_rx_buffers[PCNET_RING_SIZE][PCNET_BUFFER_SIZE] __attribute__((aligned(16)));
static u8 g_tx_buffers[PCNET_RING_SIZE][PCNET_BUFFER_SIZE] __attribute__((aligned(16)));

static void pcnet_select_csr(u16 index) { outw(g_io + PCNET_IO_RAP, index); }
static u16 pcnet_read_csr(u16 index) { pcnet_select_csr(index); return inw(g_io + PCNET_IO_RDP); }
static void pcnet_write_csr(u16 index, u16 value) { pcnet_select_csr(index); outw(g_io + PCNET_IO_RDP, value); }
static void pcnet_write_bcr(u16 index, u16 value) { outw(g_io + PCNET_IO_RAP, index); outw(g_io + PCNET_IO_BDP, value); }

static u16 pcnet_negative_length(u16 len) {
    /* The controller reads the 16-bit field as a two's-complement buffer length. */
    return (u16)(0u - (u32)len);
}

static int pcnet_wait_idon(void) {
    for (u32 spin = 0u; spin < 200000u; spin++) {
        if (pcnet_read_csr(0u) & PCNET_CSR0_IDON) return 0;
    }
    return -1;
}

static void pcnet_reset(void) {
    (void)inw(g_io + PCNET_IO_RESET);
    outw(g_io + PCNET_IO_RESET, 0u);
    pcnet_write_csr(0u, PCNET_CSR0_STOP);
    /* BCR20=2 chooses the 32-bit descriptor and initialization format. */
    pcnet_write_bcr(20u, 2u);
}

static void pcnet_prepare_rings(void) {
    memset(&g_init, 0, sizeof(g_init));
    memset(g_rx, 0, sizeof(g_rx));
    memset(g_tx, 0, sizeof(g_tx));
    for (u32 i = 0u; i < PCNET_RING_SIZE; i++) {
        g_rx[i].base = (u32)g_rx_buffers[i];
        g_rx[i].buf_len = pcnet_negative_length(PCNET_BUFFER_SIZE);
        g_rx[i].status = PCNET_DESC_OWN;
        g_tx[i].base = (u32)g_tx_buffers[i];
        g_tx[i].buf_len = pcnet_negative_length(PCNET_BUFFER_SIZE);
    }
    g_init.rlen = (u8)(PCNET_RING_LOG2 << 4u);
    g_init.tlen = (u8)(PCNET_RING_LOG2 << 4u);
    memcpy(g_init.mac, g_mac, sizeof(g_mac));
    g_init.rx_ring = (u32)g_rx;
    g_init.tx_ring = (u32)g_tx;
    g_rx_index = 0u;
    g_tx_index = 0u;
}

int pcnet_init(void) {
    pci_device_t dev;
    g_ready = 0u;
    g_io = 0u;
    if (pci_find_device(PCNET_VENDOR, PCNET_DEVICE, &dev) != 0) return -1;
    if ((dev.bar0 & 1u) == 0u) return -1;
    g_io = (u16)(dev.bar0 & 0xFFFCu);
    if (!g_io || pci_enable_bus_master(&dev) != 0) return -1;

    pcnet_reset();
    for (u32 i = 0u; i < sizeof(g_mac); i++) g_mac[i] = inb(g_io + i);
    pcnet_prepare_rings();
    pcnet_write_csr(1u, (u16)((u32)&g_init & 0xFFFFu));
    pcnet_write_csr(2u, (u16)(((u32)&g_init >> 16u) & 0xFFFFu));
    pcnet_write_csr(0u, PCNET_CSR0_INIT);
    if (pcnet_wait_idon() != 0) return -1;
    pcnet_write_csr(0u, PCNET_CSR0_IDON);
    pcnet_write_csr(0u, PCNET_CSR0_START | PCNET_CSR0_IENA);
    g_ready = 1u;
    return 0;
}

int pcnet_ready(void) { return g_ready ? 1 : 0; }

int pcnet_get_mac(u8* out_mac) {
    if (!out_mac || !g_ready) return -1;
    memcpy(out_mac, g_mac, sizeof(g_mac));
    return 0;
}

int pcnet_send(const u8* frame, u16 len) {
    if (!g_ready || !frame || len < 14u || len > PCNET_BUFFER_SIZE) return -1;
    pcnet_desc_t* d = &g_tx[g_tx_index];
    if (d->status & PCNET_DESC_OWN) return -2;
    memcpy(g_tx_buffers[g_tx_index], frame, len);
    d->buf_len = pcnet_negative_length(len);
    d->msg_len = 0u;
    d->status = PCNET_DESC_OWN | PCNET_DESC_STP | PCNET_DESC_ENP;
    g_tx_index = (g_tx_index + 1u) % PCNET_RING_SIZE;
    pcnet_write_csr(0u, PCNET_CSR0_TDMD | PCNET_CSR0_IENA);
    return 0;
}

u32 pcnet_poll(u32 budget, pcnet_rx_handler_t handler) {
    u32 handled = 0u;
    if (!g_ready || !handler) return 0u;
    while (handled < budget) {
        pcnet_desc_t* d = &g_rx[g_rx_index];
        if (d->status & PCNET_DESC_OWN) break;
        u16 length = (u16)(d->msg_len & 0x0FFFu);
        if ((d->status & (PCNET_DESC_ERR | PCNET_DESC_STP | PCNET_DESC_ENP)) ==
            (PCNET_DESC_STP | PCNET_DESC_ENP) && length >= 14u && length <= PCNET_BUFFER_SIZE) {
            (void)handler(g_rx_buffers[g_rx_index], length);
            handled++;
        }
        d->msg_len = 0u;
        d->buf_len = pcnet_negative_length(PCNET_BUFFER_SIZE);
        d->status = PCNET_DESC_OWN;
        g_rx_index = (g_rx_index + 1u) % PCNET_RING_SIZE;
    }
    u16 status = pcnet_read_csr(0u);
    if (status & (PCNET_CSR0_RINT | PCNET_CSR0_TINT)) pcnet_write_csr(0u, status & (PCNET_CSR0_RINT | PCNET_CSR0_TINT));
    return handled;
}
