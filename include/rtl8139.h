#ifndef RTL8139_H
#define RTL8139_H

#include "types.h"

#define RTL8139_VENDOR_ID   0x10EC
#define RTL8139_DEVICE_ID   0x8139

#define RTL8139_REG_MAC0    0x00
#define RTL8139_REG_MAR0    0x08
#define RTL8139_REG_RBSTART 0x30
#define RTL8139_REG_CMD     0x37
#define RTL8139_REG_IMR     0x3C
#define RTL8139_REG_ISR     0x3E
#define RTL8139_REG_TCR     0x40
#define RTL8139_REG_RCR     0x44

void rtl8139_init(void);
int rtl8139_available(void);
int rtl8139_send_packet(const u8* data, u16 len);
int rtl8139_recv_packet(u8* out_buf, u16 max_len);
void rtl8139_get_mac(u8 mac[6]);

#endif /* RTL8139_H */
