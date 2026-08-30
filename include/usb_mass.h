#ifndef DRIVERS_USB_MASS_H
#define DRIVERS_USB_MASS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_CLASS_MASS_STORAGE   0x08u
#define USB_SUBCLASS_SCSI        0x06u
#define USB_PROTOCOL_BOT         0x50u /* Bulk-Only Transport */

#define USB_CBW_SIGNATURE        0x43425355u /* "USBC" */
#define USB_CSW_SIGNATURE        0x53425355u /* "USBS" */

#define USB_CBW_FLAG_IN          0x80u       /* Data-In (Device to Host) */
#define USB_CBW_FLAG_OUT         0x00u       /* Data-Out (Host to Device) */

#define USB_CSW_STATUS_PASSED    0x00u
#define USB_CSW_STATUS_FAILED    0x01u
#define USB_CSW_STATUS_PHASE_ERR 0x02u

/* SCSI Standard OpCodes */
#define SCSI_TEST_UNIT_READY     0x00u
#define SCSI_REQUEST_SENSE       0x03u
#define SCSI_INQUIRY             0x12u
#define SCSI_MODE_SENSE_6        0x1Au
#define SCSI_START_STOP_UNIT     0x1Bu
#define SCSI_READ_CAPACITY_10    0x25u
#define SCSI_READ_10             0x28u
#define SCSI_WRITE_10            0x2Au
#define SCSI_READ_CAPACITY_16    0x9Eu

/* 31-Byte Command Block Wrapper (CBW) */
typedef struct {
    u32 dCBWSignature;          /* "USBC" */
    u32 dCBWTag;                /* Unique transaction identifier */
    u32 dCBWDataTransferLength; /* Expected byte count to transfer */
    u8  bmCBWFlags;             /* Bit 7: 0=Out, 1=In */
    u8  bCBWLUN;                /* Logical Unit Number (0..15) */
    u8  bCBWCBLength;           /* Valid length of CDB (1..16) */
    u8  CBWCB[16];              /* SCSI Command Descriptor Block */
} __attribute__((packed)) usb_cbw_t;

/* 13-Byte Command Status Wrapper (CSW) */
typedef struct {
    u32 dCSWSignature;          /* "USBS" */
    u32 dCSWTag;                /* Must match dCBWTag */
    u32 dCSWDataResidue;        /* Difference between expected and actual bytes */
    u8  bCSWStatus;             /* 0=Passed, 1=Failed, 2=Phase Error */
} __attribute__((packed)) usb_csw_t;

/* 36-Byte SCSI Standard INQUIRY Response */
typedef struct {
    u8  peripheral_device_type; /* 0x00 = Direct access block device (Disk) */
    u8  rmb;                    /* Bit 7: Removable Medium */
    u8  version;                /* ANSI SPC version */
    u8  response_data_format;
    u8  additional_length;      /* N - 4 (typically 31) */
    u8  flags1;
    u8  flags2;
    u8  flags3;
    char vendor_id[8];          /* T10 Vendor Identification (ASCII) */
    char product_id[16];        /* Product Identification (ASCII) */
    char product_rev[4];        /* Product Revision Level (ASCII) */
} __attribute__((packed)) scsi_inquiry_resp_t;

/* 8-Byte SCSI READ CAPACITY (10) Response */
typedef struct {
    u32 last_lba_be;            /* Big-Endian Last Logical Block Address */
    u32 block_length_be;        /* Big-Endian Block Length in Bytes */
} __attribute__((packed)) scsi_read_capacity10_resp_t;

/* 18-Byte SCSI REQUEST SENSE Response */
typedef struct {
    u8  response_code;          /* 0x70 = Current error */
    u8  segment_number;
    u8  sense_key;              /* Sense Key: 0x00=No Sense, 0x02=Not Ready, 0x03=Medium Error */
    u32 information;
    u8  additional_sense_len;   /* Typically 10 */
    u32 cmd_specific_info;
    u8  additional_sense_code;  /* ASC */
    u8  asc_qualifier;          /* ASCQ */
    u8  fru_code;
    u8  sense_key_specific[3];
} __attribute__((packed)) scsi_request_sense_resp_t;

/* In-Memory USB Mass Storage State */
typedef struct {
    u8   is_attached;
    u8   lun;
    u64  sector_count;
    u32  sector_size;
    char vendor[9];
    char product[17];
    char revision[5];
} usb_mass_device_t;

/* Public API */
int  usb_mass_init(void);
int  usb_mass_test_unit_ready(void);
int  usb_mass_inquiry(scsi_inquiry_resp_t* out_inq);
int  usb_mass_read_capacity(u64* out_sector_count, u32* out_sector_size);
int  usb_mass_request_sense(scsi_request_sense_resp_t* out_sense);
int  usb_mass_read_sector(u32 lba, u8* buffer);
int  usb_mass_write_sector(u32 lba, const u8* buffer);
int  usb_mass_reset_recovery(void);
int  usb_mass_get_device_info(char* out_vendor, char* out_product, char* out_revision, u32* out_capacity_mb);
void usb_mass_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_USB_MASS_H */
