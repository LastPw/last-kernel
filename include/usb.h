#ifndef USB_H
#define USB_H

#include "types.h"

#define USB_MAX_ROOT_PORTS 15u

typedef struct {
    u8 ohci_found;
    u8 ehci_found;
    u8 xhci_found;
    u8 ehci_ready;
    u8 controller_ready;
    u8 port_count;
    u16 vendor;
    u16 device;
    u32 mmio_base;
    u32 connected_mask;
    u32 low_speed_mask;
    u32 reset_failures;
    u8 hid_keyboard;
    u8 hid_mouse;
    u8 hid_address;
    u8 hid_endpoint;
    u16 hid_packet_size;
    u32 enumeration_failures;
    u32 hid_reports;
    u32 last_error;
    u8 mass_storage_found;
    u8 mass_storage_ready;
    u8 mass_address;
    u8 mass_in_endpoint;
    u8 mass_out_endpoint;
    u16 mass_in_packet;
    u16 mass_out_packet;
    u32 mass_block_count;
    u32 mass_block_size;
    u8 controller_kind;
    u32 enumerated_ports;
} usb_status_t;

int usb_init(void);
int usb_ready(void);
void usb_poll(void);
void usb_status(usb_status_t* out);
int usb_mass_storage_ready(void);
u32 usb_mass_storage_sector_count(void);
int usb_mass_storage_read_sector(u32 lba, u8* out512);
int usb_mass_storage_write_sector(u32 lba, const u8* in512);

#endif
