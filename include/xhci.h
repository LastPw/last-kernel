#ifndef XHCI_H
#define XHCI_H

#include "types.h"

/* ─── PCI class codes ─── */
#define PCI_CLASS_SERIAL_BUS  0x0Cu
#define PCI_SUBCLASS_USB      0x03u
#define PCI_PROGIF_XHCI       0x30u

/* ─── Capability Registers ─── */
#define XHCI_CAP_CAPLENGTH     0x00u
#define XHCI_CAP_HCIVERSION    0x02u
#define XHCI_CAP_HCSPARAMS1    0x04u
#define XHCI_CAP_HCSPARAMS2    0x08u
#define XHCI_CAP_HCSPARAMS3    0x0Cu
#define XHCI_CAP_HCCPARAMS1    0x10u
#define XHCI_CAP_DBOFF         0x14u
#define XHCI_CAP_RTSOFF        0x18u

#define XHCI_HCS1_MAX_SLOTS(v)      ((v) & 0xFFu)
#define XHCI_HCS1_MAX_INTRS(v)      (((v) >> 8) & 0x3FFu)
#define XHCI_HCS1_MAX_PORTS(v)      (((v) >> 24) & 0xFFu)
#define XHCI_HCC1_CSZ               (1u << 2)

/* ─── Operational Registers ─── */
#define XHCI_OP_USBCMD     0x00u
#define XHCI_OP_USBSTS     0x04u
#define XHCI_OP_PAGESIZE   0x08u
#define XHCI_OP_DNCTRL     0x14u
#define XHCI_OP_CRCR       0x18u
#define XHCI_OP_DCBAAP     0x30u
#define XHCI_OP_CONFIG     0x38u
#define XHCI_OP_PORTSC     0x400u

#define XHCI_CMD_RUN       (1u << 0)
#define XHCI_CMD_RESET     (1u << 1)
#define XHCI_CMD_INTE      (1u << 2)
#define XHCI_CMD_HSEE      (1u << 3)
#define XHCI_STS_HCH       (1u << 0)
#define XHCI_STS_HSE       (1u << 2)
#define XHCI_STS_EINT      (1u << 3)
#define XHCI_STS_PCD       (1u << 4)
#define XHCI_STS_CNR       (1u << 11)
#define XHCI_CRCR_RCS      (1u << 0)
#define XHCI_CRCR_CA       (1u << 2)
#define XHCI_CRCR_CRR      (1u << 3)

/* PORTSC */
#define XHCI_PORTSC_CCS        (1u << 0)
#define XHCI_PORTSC_PED        (1u << 1)
#define XHCI_PORTSC_PR         (1u << 4)
#define XHCI_PORTSC_PP         (1u << 9)
#define XHCI_PORTSC_SPEED_SHIFT 10u
#define XHCI_PORTSC_CSC        (1u << 17)
#define XHCI_PORTSC_PRC        (1u << 21)
#define XHCI_PORTSC_PLC        (1u << 22)
#define XHCI_PORTSC_OCC        (1u << 20)
#define XHCI_SPEED_FULL   1u
#define XHCI_SPEED_LOW    2u
#define XHCI_SPEED_HIGH   3u
#define XHCI_SPEED_SUPER  4u

/* ─── Doorbell ─── */
#define XHCI_DB_HOST     0u
#define XHCI_DB_CMD      0u

/* ─── Interrupter Registers ─── */
#define XHCI_IR_IMAN     0x00u
#define XHCI_IR_IMOD     0x04u
#define XHCI_IR_ERSTSZ   0x08u
#define XHCI_IR_ERSTBA   0x10u
#define XHCI_IR_ERDP     0x18u
#define XHCI_IMAN_IP     (1u << 0)
#define XHCI_IMAN_IE     (1u << 1)

/* ─── TRB ─── */
#define XHCI_TRB_SIZE    16u
#define TRB_NORMAL              1u
#define TRB_SETUP_STAGE         2u
#define TRB_DATA_STAGE          3u
#define TRB_STATUS_STAGE        4u
#define TRB_LINK                6u
#define TRB_CMD_ENABLE_SLOT     9u
#define TRB_CMD_DISABLE_SLOT   10u
#define TRB_CMD_ADDRESS_DEV    11u
#define TRB_CMD_CONFIGURE_EP   12u
#define TRB_CMD_EVALUATE_CTX   13u
#define TRB_CMD_RESET_EP       14u
#define TRB_EVT_TRANSFER       32u
#define TRB_EVT_CMD_COMPLETE   33u
#define TRB_EVT_PORT_STATUS    34u

#define TRB_C                (1u << 0)
#define TRB_TC               (1u << 1)
#define TRB_CHAIN            (1u << 4)
#define TRB_IOC              (1u << 5)
#define TRB_ISP              (1u << 2)
#define TRB_ENT              (1u << 6)
#define TRB_TYPE_SHIFT       10u
#define TRB_TYPE(t)          (((u32)(t) & 0x3Fu) << 10u)

#define TRB_DIR_IN           (1u << 16)

typedef struct __attribute__((packed, aligned(16))) {
    u32 params[4];
} xhci_trb_t;

/* ─── Contexts (32-byte or 64-byte) ─── */
typedef struct __attribute__((packed, aligned(64))) {
    u32 ent[4];
} xhci_slot_ctx_t;

typedef struct __attribute__((packed, aligned(64))) {
    u32 ent[4];
} xhci_ep_ctx_t;

typedef struct __attribute__((packed, aligned(64))) {
    xhci_slot_ctx_t slot;
    xhci_ep_ctx_t   eps[31];
} xhci_dev_ctx_t;

typedef struct __attribute__((packed, aligned(64))) {
    u32 drop_flags;
    u32 add_flags;
    u32 rsvd[6];
    xhci_slot_ctx_t slot;
} xhci_input_ctx_t;

/* Input context header: drop_flags, add_flags, rsvd[6] = 32 bytes */
#define XHCI_INPUT_CTX_HEADER_SZ 32u

/* ─── ERST ─── */
typedef struct __attribute__((packed, aligned(16))) {
    u32 base_low;
    u32 base_high;
    u32 size;
    u32 rsvd;
} xhci_erst_t;

/* ─── USB ─── */
#define USB_DIR_IN          0x80u
#define USB_DIR_OUT         0x00u
#define USB_REQ_GET_DESCRIPTOR    6u
#define USB_REQ_SET_ADDRESS       5u
#define USB_REQ_SET_CONFIG        9u
#define USB_DESC_DEVICE           1u
#define USB_DESC_CONFIG           2u
#define USB_DESC_INTERFACE        4u
#define USB_DESC_ENDPOINT         5u
#define USB_CLASS_HID             3u
#define USB_HID_SUBCLASS_BOOT     1u
#define USB_HID_PROTOCOL_KEYBOARD 1u
#define USB_HID_PROTOCOL_MOUSE    2u
#define USB_REQ_SET_PROTOCOL      0x0Bu
#define USB_REQ_SET_IDLE          0x0Au

/* ─── Limits ─── */
#define XHCI_MAX_SLOTS   16u
#define XHCI_MAX_PORTS   15u
#define XHCI_CMD_RING_SZ 32u
#define XHCI_EVT_RING_SZ 32u
#define XHCI_TRANSFER_RING_SZ 16u
#define XHCI_TRB_BUF_SIZE 256u

/* ─── API ─── */
int  xhci_init(void);
int  xhci_available(void);
void xhci_poll(void);
int  xhci_hid_keyboard_ready(void);
int  xhci_hid_mouse_ready(void);
void xhci_mouse_state(i32* dx, i32* dy, u8* buttons);

#endif /* XHCI_H */
