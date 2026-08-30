#ifndef E1000_H
#define E1000_H

#include "types.h"

#define INTEL_VNDR_ID         0x8086u

/* ─── Intel E1000 / E1000e / I21x Device Matrix ─── */
#define E1000_DEV_82540EM     0x100Eu
#define E1000_DEV_82545EM     0x100Fu
#define E1000_DEV_82574L      0x10D3u
#define E1000_DEV_82577LM     0x10EAu
#define E1000_DEV_82577LC     0x10EBu
#define E1000_DEV_82578DM     0x10EFu
#define E1000_DEV_82578DC     0x10F0u
#define E1000_DEV_82579LM     0x1502u
#define E1000_DEV_82579V      0x1503u
#define E1000_DEV_I217_LM     0x153Au
#define E1000_DEV_I217_V      0x153Bu
#define E1000_DEV_I218_LM     0x155Au
#define E1000_DEV_I218_V      0x1559u
#define E1000_DEV_I218_LM2    0x15A0u
#define E1000_DEV_I218_V2     0x15A1u
#define E1000_DEV_I218_LM3    0x15A2u
#define E1000_DEV_I218_V3     0x15A3u
#define E1000_DEV_I219_LM1    0x156Fu
#define E1000_DEV_I219_V1     0x1570u
#define E1000_DEV_I219_LM2    0x15B7u
#define E1000_DEV_I219_V2     0x15B8u
#define E1000_DEV_I219_LM3    0x15BBu
#define E1000_DEV_I219_V3     0x15BCu
#define E1000_DEV_I219_LM4    0x15DFu
#define E1000_DEV_I219_V4     0x15E3u
#define E1000_DEV_I219_LM5    0x15F9u
#define E1000_DEV_I219_V5     0x15FAu
#define E1000_DEV_I210        0x1533u
#define E1000_DEV_I211        0x1539u

/* ─── E1000 Register Offsets ─── */
#define E1000_REG_CTRL        0x0000u
#define E1000_REG_STATUS      0x0008u
#define E1000_REG_EERD        0x0014u
#define E1000_REG_ICR         0x00C0u
#define E1000_REG_IMS         0x00D0u
#define E1000_REG_IMC         0x00D8u
#define E1000_REG_RCTL        0x0100u
#define E1000_REG_TCTL        0x0400u
#define E1000_REG_RDBAL       0x2800u
#define E1000_REG_RDBAH       0x2804u
#define E1000_REG_RDLEN       0x2808u
#define E1000_REG_RDH         0x2810u
#define E1000_REG_RDT         0x2818u
#define E1000_REG_TDBAL       0x3800u
#define E1000_REG_TDBAH       0x3804u
#define E1000_REG_TDLEN       0x3808u
#define E1000_REG_TDH         0x3810u
#define E1000_REG_TDT         0x3818u
#define E1000_REG_RAL0        0x5400u
#define E1000_REG_RAH0        0x5404u

/* ─── RCTL & TCTL Flag Bits ─── */
#define E1000_RCTL_EN         (1u << 1)
#define E1000_RCTL_SBP        (1u << 2)
#define E1000_RCTL_UPE        (1u << 3)
#define E1000_RCTL_MPE        (1u << 4)
#define E1000_RCTL_LPE        (1u << 5)
#define E1000_RCTL_BAM        (1u << 15)
#define E1000_RCTL_SECRC      (1u << 26)
#define E1000_RCTL_BSIZE_2048 (0u << 16)

#define E1000_TCTL_EN         (1u << 1)
#define E1000_TCTL_PSP        (1u << 3)
#define E1000_TCTL_CT_SHIFT   4u
#define E1000_TCTL_COLD_SHIFT 12u

#define E1000_NUM_RX_DESC     32u
#define E1000_NUM_TX_DESC     32u

typedef struct {
    u64 addr;
    u16 length;
    u16 checksum;
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    u64 addr;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
} __attribute__((packed)) e1000_tx_desc_t;

void e1000_init(void);
int  e1000_available(void);
int  e1000_send_packet(const u8* data, u16 len);
int  e1000_recv_packet(u8* out_buf, u16 max_len);
void e1000_get_mac(u8 mac[6]);

#endif /* E1000_H */
