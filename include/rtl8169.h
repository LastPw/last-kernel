#ifndef RTL8169_H
#define RTL8169_H

#include "types.h"

#define RTL8169_VENDOR_ID   0x10ECu
#define RTL8169_DEVICE_ID   0x8169u

void rtl8169_init(void);
int  rtl8169_available(void);
void rtl8169_get_mac(u8 mac[6]);
int  rtl8169_send_packet(const u8* data, u16 len);
int  rtl8169_recv_packet(u8* out_buf, u16 max_len);

#endif
