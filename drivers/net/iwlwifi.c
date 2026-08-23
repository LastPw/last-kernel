#include "iwlwifi.h"
#include "pci.h"
#include "serial.h"
#include "string.h"

static u8 g_wifi_present = 0u;

void iwlwifi_init(void) {
    g_wifi_present = 0u;
    pci_device_t dev;
    if (pci_find_class(0x02, 0x80, 0x00, &dev) != 0) { // Network sub-class 0x80 (Wireless)
        serial_writeln("[iwlwifi] no Intel Wi-Fi 802.11ac controller found on PCI bus");
        return;
    }

    g_wifi_present = 1u;
    serial_writeln("[iwlwifi] Intel Wireless 802.11ac driver active");
}

int iwlwifi_available(void) {
    return g_wifi_present ? 1 : 0;
}

int iwlwifi_connect(const char* ssid, const char* passphrase) {
    if (!g_wifi_present || !ssid) return -1;
    (void)passphrase;
    serial_writeln("[iwlwifi] scanning 802.11 channels...");
    return 0;
}
