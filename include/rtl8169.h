#ifndef RTL8169_H
#define RTL8169_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTL8169_VENDOR_ID       0x10EC
#define RTL8169_DEVICE_ID       0x8169
#define RTL8168_DEVICE_ID       0x8168

#define RTL8169_NUM_TX_DESC     32u
#define RTL8169_NUM_RX_DESC     32u
#define RTL8169_BUFFER_SIZE     2048u

#define RTL8169_DESC_OWN        (1u << 31) /* Hardware owns descriptor */
#define RTL8169_DESC_EOR        (1u << 30) /* End of Ring */
#define RTL8169_DESC_FS         (1u << 29) /* First Segment */
#define RTL8169_DESC_LS         (1u << 28) /* Last Segment */

/* 16-Byte Hardware Descriptor */
typedef struct {
    u32 opts1;                  /* Flags + Length */
    u32 opts2;                  /* VLAN & Checksum */
    u32 addr_low;               /* 64-bit DMA buffer address low */
    u32 addr_high;              /* 64-bit DMA buffer address high */
} __attribute__((packed)) rtl8169_desc_t;

/* Public API */
void rtl8169_init(void);
int  rtl8169_available(void);
int  rtl8169_send_packet(const u8* data, u16 len);
int  rtl8169_recv_packet(u8* out_buf, u16 max_len);
void rtl8169_get_mac(u8 mac[6]);
void rtl8169_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* RTL8169_H */
