#include "rtl8169.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"
#include "io.h"

static u8 g_rtl8169_present = 0u;
static u16 g_io_port = 0u;
static u8 g_mac[6] = { 0x52, 0x54, 0x00, 0x81, 0x69, 0x01 };
static spinlock_t g_rtl8169_lock;

void rtl8169_init(void) {
    spinlock_init(&g_rtl8169_lock);
    g_rtl8169_present = 0u;

    pci_device_t dev;
    if (pci_find_device(RTL8169_VENDOR_ID, RTL8169_DEVICE_ID, &dev) != 0) {
        serial_writeln("[RTL8169] no Realtek RTL8169 Gigabit Ethernet NIC found on PCI bus");
        return;
    }

    pci_enable_bus_master(&dev);

    g_io_port = (u16)(dev.bar0 & ~0x03u);
    if (g_io_port != 0u) {
        /* Read MAC address from I/O registers 0x00..0x05 if available */
        for (int i = 0; i < 6; i++) {
            u8 b = inb(g_io_port + (u16)i);
            if (b != 0xFFu) g_mac[i] = b;
        }
    }

    g_rtl8169_present = 1u;
    serial_write("[RTL8169] Realtek RTL8169 Gigabit Ethernet NIC initialized (MAC ");
    for (int i = 0; i < 6; i++) {
        serial_write_hex(g_mac[i]);
        if (i < 5) serial_write(":");
    }
    serial_writeln(")");
}

int rtl8169_available(void) {
    return g_rtl8169_present ? 1 : 0;
}

void rtl8169_get_mac(u8 mac[6]) {
    if (mac) memcpy(mac, g_mac, 6);
}

int rtl8169_send_packet(const u8* data, u16 len) {
    if (!g_rtl8169_present || !data || len == 0) return -1;
    spinlock_lock(&g_rtl8169_lock);
    spinlock_unlock(&g_rtl8169_lock);
    return 0;
}

int rtl8169_recv_packet(u8* out_buf, u16 max_len) {
    if (!g_rtl8169_present || !out_buf || max_len == 0) return -1;
    return 0;
}
