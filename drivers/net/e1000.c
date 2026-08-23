#include "e1000.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "heap.h"
#include "spinlock.h"
#include "paging.h"

static u8 g_e1000_present = 0u;
static uintptr_t g_mmio_base = 0;
static u8 g_mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

static e1000_rx_desc_t g_rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
static e1000_tx_desc_t g_tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));
static u8 g_rx_buffers[E1000_NUM_RX_DESC][2048] __attribute__((aligned(16)));
static u8 g_tx_buffers[E1000_NUM_TX_DESC][2048] __attribute__((aligned(16)));

static u32 g_rx_tail = 0u;
static u32 g_tx_tail = 0u;
static spinlock_t g_e1000_lock;

static const u16 s_intel_device_ids[] = {
    E1000_DEV_82540EM,
    E1000_DEV_82545EM,
    E1000_DEV_82574L,
    E1000_DEV_82577LM,
    E1000_DEV_82577LC,
    E1000_DEV_82578DM,
    E1000_DEV_82578DC,
    E1000_DEV_82579LM,
    E1000_DEV_82579V,
    E1000_DEV_I217_LM,
    E1000_DEV_I217_V,
    E1000_DEV_I218_LM,
    E1000_DEV_I218_V,
    E1000_DEV_I218_LM2,
    E1000_DEV_I218_V2,
    E1000_DEV_I218_LM3,
    E1000_DEV_I218_V3,
    E1000_DEV_I219_LM1,
    E1000_DEV_I219_V1,
    E1000_DEV_I219_LM2,
    E1000_DEV_I219_V2,
    E1000_DEV_I219_LM3,
    E1000_DEV_I219_V3,
    E1000_DEV_I219_LM4,
    E1000_DEV_I219_V4,
    E1000_DEV_I219_LM5,
    E1000_DEV_I219_V5,
    E1000_DEV_I210,
    E1000_DEV_I211
};

static inline u32 reg_read(u32 offset) {
    return *(volatile u32*)(g_mmio_base + offset);
}

static inline void reg_write(u32 offset, u32 val) {
    *(volatile u32*)(g_mmio_base + offset) = val;
}

static u16 e1000_eeprom_read(u8 addr) {
    reg_write(E1000_REG_EERD, 1u | ((u32)addr << 8));
    for (u32 timeout = 0; timeout < 10000; timeout++) {
        u32 val = reg_read(E1000_REG_EERD);
        if (val & (1u << 4)) {
            return (u16)(val >> 16);
        }
    }
    return 0;
}

static void e1000_read_mac(void) {
    /* Try reading hardware RAL0/RAH0 registers first */
    u32 ral = reg_read(E1000_REG_RAL0);
    u32 rah = reg_read(E1000_REG_RAH0);
    if (ral != 0 && (rah & (1u << 31))) {
        g_mac[0] = (u8)(ral & 0xFF);
        g_mac[1] = (u8)((ral >> 8) & 0xFF);
        g_mac[2] = (u8)((ral >> 16) & 0xFF);
        g_mac[3] = (u8)((ral >> 24) & 0xFF);
        g_mac[4] = (u8)(rah & 0xFF);
        g_mac[5] = (u8)((rah >> 8) & 0xFF);
        return;
    }

    /* Fallback: Read from EEPROM */
    u16 val0 = e1000_eeprom_read(0);
    u16 val1 = e1000_eeprom_read(1);
    u16 val2 = e1000_eeprom_read(2);
    if (val0 || val1 || val2) {
        g_mac[0] = (u8)(val0 & 0xFF);
        g_mac[1] = (u8)(val0 >> 8);
        g_mac[2] = (u8)(val1 & 0xFF);
        g_mac[3] = (u8)(val1 >> 8);
        g_mac[4] = (u8)(val2 & 0xFF);
        g_mac[5] = (u8)(val2 >> 8);
    }
}

void e1000_init(void) {
    spinlock_init(&g_e1000_lock);
    g_e1000_present = 0u;

    pci_device_t pci_dev;
    int found = 0;
    u32 dev_id_found = 0;

    for (u32 i = 0; i < sizeof(s_intel_device_ids) / sizeof(s_intel_device_ids[0]); i++) {
        if (pci_find_device(INTEL_VNDR_ID, s_intel_device_ids[i], &pci_dev) == 0) {
            found = 1;
            dev_id_found = s_intel_device_ids[i];
            break;
        }
    }

    if (!found) {
        serial_writeln("[E1000] no Intel E1000/E1000e PCIe NIC detected");
        return;
    }

    g_mmio_base = pci_dev.bar0 & ~0x0Full;
    if (!g_mmio_base) {
        g_mmio_base = 0xE0000000ULL;
    }
    if (!paging_is_mapped(g_mmio_base)) {
        paging_map_range(g_mmio_base, g_mmio_base, 131072u, PAGE_FLAG_RW | PAGE_FLAG_PWT | PAGE_FLAG_PCD);
    }

    serial_write("[E1000] Intel Gigabit PCIe NIC detected (DevID=0x");
    serial_write_hex(dev_id_found);
    serial_writeln(")");

    /* Read MAC address */
    e1000_read_mac();

    /* Initialize RX Ring Descriptors */
    memset(g_rx_descs, 0, sizeof(g_rx_descs));
    for (u32 i = 0u; i < E1000_NUM_RX_DESC; i++) {
        g_rx_descs[i].addr = (u64)(uintptr_t)g_rx_buffers[i];
        g_rx_descs[i].status = 0u;
    }

    /* Initialize TX Ring Descriptors */
    memset(g_tx_descs, 0, sizeof(g_tx_descs));
    for (u32 i = 0u; i < E1000_NUM_TX_DESC; i++) {
        g_tx_descs[i].addr = (u64)(uintptr_t)g_tx_buffers[i];
        g_tx_descs[i].status = 1u;
    }

    g_rx_tail = 0u;
    g_tx_tail = 0u;

    /* Program hardware RX registers */
    reg_write(E1000_REG_RDBAL, (u32)(uintptr_t)g_rx_descs);
    reg_write(E1000_REG_RDBAH, (u32)((u64)(uintptr_t)g_rx_descs >> 32));
    reg_write(E1000_REG_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    reg_write(E1000_REG_RDH, 0);
    reg_write(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    reg_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
                              E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC |
                              E1000_RCTL_BSIZE_2048);

    /* Program hardware TX registers */
    reg_write(E1000_REG_TDBAL, (u32)(uintptr_t)g_tx_descs);
    reg_write(E1000_REG_TDBAH, (u32)((u64)(uintptr_t)g_tx_descs >> 32));
    reg_write(E1000_REG_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    reg_write(E1000_REG_TDH, 0);
    reg_write(E1000_REG_TDT, 0);
    reg_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP |
                              (15u << E1000_TCTL_CT_SHIFT) |
                              (64u << E1000_TCTL_COLD_SHIFT));

    g_e1000_present = 1u;

    serial_write("[E1000] Initialized with MAC: ");
    for (int m = 0; m < 6; m++) {
        serial_write_hex((u32)g_mac[m]);
        if (m < 5) serial_putc(':');
    }
    serial_writeln("");
}

int e1000_available(void) {
    return g_e1000_present ? 1 : 0;
}

void e1000_get_mac(u8 mac[6]) {
    if (mac) memcpy(mac, g_mac, 6);
}

int e1000_send_packet(const u8* data, u16 len) {
    if (!g_e1000_present || !data || len == 0 || len > 2048) return -1;
    spinlock_lock(&g_e1000_lock);

    u32 idx = g_tx_tail;
    memcpy(g_tx_buffers[idx], data, len);
    g_tx_descs[idx].length = len;
    g_tx_descs[idx].cmd = (1u << 0) | (1u << 1) | (1u << 3); /* EOP | IFCS | RS */
    g_tx_descs[idx].status = 0u;

    g_tx_tail = (g_tx_tail + 1u) % E1000_NUM_TX_DESC;
    reg_write(E1000_REG_TDT, g_tx_tail);
    spinlock_unlock(&g_e1000_lock);
    return 0;
}

int e1000_recv_packet(u8* out_buf, u16 max_len) {
    if (!g_e1000_present || !out_buf || max_len == 0) return -1;
    spinlock_lock(&g_e1000_lock);

    u32 idx = g_rx_tail;
    if (!(g_rx_descs[idx].status & 0x01u)) {
        spinlock_unlock(&g_e1000_lock);
        return 0; /* No packet */
    }

    u16 len = g_rx_descs[idx].length;
    if (len > max_len) len = max_len;
    memcpy(out_buf, g_rx_buffers[idx], len);

    g_rx_descs[idx].status = 0u;
    g_rx_tail = (g_rx_tail + 1u) % E1000_NUM_RX_DESC;
    reg_write(E1000_REG_RDT, (g_rx_tail + E1000_NUM_RX_DESC - 1u) % E1000_NUM_RX_DESC);

    spinlock_unlock(&g_e1000_lock);
    return (int)len;
}
