#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "types.h"

typedef struct {
    u8 mac_addr[6];
    char device_name[32];
    s8 rssi;
    u8 connected;
    u8 paired;
} bluetooth_device_t;

typedef struct {
    u8 hci_version; // 0x0C = Bluetooth 5.3
    u16 hci_revision;
    u8 lmp_version;
    u16 manufacturer;
    u8 active_devices_count;
    bluetooth_device_t devices[8];
} bluetooth_adapter_t;

int bluetooth_init(void);
int bluetooth_start_discovery(void);
int bluetooth_get_devices(bluetooth_device_t* out_devices, u32 max_devices);
int bluetooth_connect_device(const u8 mac_addr[6]);

#endif
