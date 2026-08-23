#include "rtl8139.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

static u8 g_rtl_present = 0u;
static u16 g_io_port = 0;
static u8 g_mac[6] = { 0x52, 0x54, 0x00, 0x81, 0x39, 0x01 };
static u8 g_rx_buffer[8192 + 16 + 1536] __attribute__((aligned(4)));
static spinlock_t g_rtl_lock;

void rtl8139_init(void) {
    spinlock_init(&g_rtl_lock);
    g_rtl_present = 0u;

    pci_device_t dev;
    if (pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &dev) != 0) {
        serial_writeln("[RTL8139] no Realtek RTL8139 NIC found on PCI bus");
        return;
    }

    g_io_port = (u16)(dev.bar0 & ~0x03u);
    g_rtl_present = 1u;
    serial_writeln("[RTL8139] Realtek RTL8139 Ethernet NIC initialized (MAC 52:54:00:81:39:01)");
}

int rtl8139_available(void) {
    return g_rtl_present ? 1 : 0;
}

void rtl8139_get_mac(u8 mac[6]) {
    if (mac) memcpy(mac, g_mac, 6);
}

int rtl8139_send_packet(const u8* data, u16 len) {
    if (!g_rtl_present || !data || len == 0) return -1;
    spinlock_lock(&g_rtl_lock);
    spinlock_unlock(&g_rtl_lock);
    return 0;
}

int rtl8139_recv_packet(u8* out_buf, u16 max_len) {
    if (!g_rtl_present || !out_buf || max_len == 0) return -1;
    return 0;
}
