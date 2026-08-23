#ifndef USB_MSC_H
#define USB_MSC_H

#include "types.h"

/* USB Mass Storage Class Definitions */
#define USB_CLASS_MASS_STORAGE    0x08u
#define USB_SUBCLASS_SCSI         0x06u
#define USB_PROTOCOL_BOT          0x50u

#define USB_MSC_CBW_SIGNATURE     0x43425355u /* "USBC" */
#define USB_MSC_CSW_SIGNATURE     0x53425355u /* "USBS" */

#define USB_MSC_DIR_OUT           0x00u
#define USB_MSC_DIR_IN            0x80u

#define USB_MSC_CSW_STATUS_PASSED 0x00u
#define USB_MSC_CSW_STATUS_FAILED 0x01u
#define USB_MSC_CSW_STATUS_PHASE  0x02u

/* SCSI Opcodes */
#define SCSI_TEST_UNIT_READY      0x00u
#define SCSI_REQUEST_SENSE        0x03u
#define SCSI_INQUIRY              0x12u
#define SCSI_MODE_SENSE_6         0x1Au
#define SCSI_START_STOP_UNIT      0x1Bu
#define SCSI_READ_CAPACITY_10     0x25u
#define SCSI_READ_10              0x28u
#define SCSI_WRITE_10             0x2Au

typedef struct __attribute__((packed)) {
    u32 signature;          /* 0x43425355 ("USBC") */
    u32 tag;                /* Unique transaction tag */
    u32 data_transfer_len;  /* Number of bytes to transfer */
    u8  flags;              /* 0x80 = IN, 0x00 = OUT */
    u8  lun;                /* Logical Unit Number */
    u8  cmd_len;            /* Length of SCSI CDB (1..16) */
    u8  cdb[16];            /* SCSI Command Descriptor Block */
} usb_msc_cbw_t;

typedef struct __attribute__((packed)) {
    u32 signature;          /* 0x53425355 ("USBS") */
    u32 tag;                /* Echoed tag */
    u32 data_residue;       /* Difference in bytes transferred */
    u8  status;             /* 0 = Passed, 1 = Failed, 2 = Phase Error */
} usb_msc_csw_t;

typedef struct {
    u8   present;
    u8   ready;
    u8   ep_in;
    u8   ep_out;
    u8   slot_id;
    u8   lun_count;
    u32  block_count;
    u32  block_size;
    char vendor[9];
    char product[17];
    char revision[5];
} usb_msc_device_t;

void usb_msc_init(void);
int  usb_msc_register_device(u8 slot_id, u8 ep_in, u8 ep_out);
int  usb_msc_is_ready(void);
u32  usb_msc_sector_count(void);
u32  usb_msc_sector_size(void);
int  usb_msc_read_sector(u32 lba, u8* out512);
int  usb_msc_write_sector(u32 lba, const u8* in512);
void usb_msc_get_info(usb_msc_device_t* out);

#endif /* USB_MSC_H */
