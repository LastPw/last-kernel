#ifndef DRIVERS_BLOCK_AHCI_H
#define DRIVERS_BLOCK_AHCI_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AHCI_PCI_CLASS_STORAGE   0x01u
#define AHCI_PCI_SUBCLASS_SATA   0x06u
#define AHCI_PCI_PROGIF_AHCI     0x01u

#define AHCI_MAX_PORTS           32u
#define AHCI_MAX_CMDS            32u
#define AHCI_MAX_PRDT_ENTRIES    8u

#define HBA_PORT_IPM_ACTIVE      1u
#define HBA_PORT_DET_PRESENT     3u

#define SATA_SIG_ATA             0x00000101u /* SATA Drive */
#define SATA_SIG_ATAPI           0xEB140101u /* SATAPI Optical Drive */
#define SATA_SIG_SEMB            0xC33C0101u /* Enclosure Management */
#define SATA_SIG_PM              0x96690101u /* Port Multiplier */

#define HBA_GHC_AE               (1u << 31)  /* AHCI Enable */
#define HBA_GHC_HR               (1u << 0)   /* HBA Reset */
#define HBA_GHC_IE               (1u << 1)   /* Interrupt Enable */

#define HBA_PxCMD_ST             (1u << 0)   /* Start */
#define HBA_PxCMD_SUD            (1u << 1)   /* Spin-Up Device */
#define HBA_PxCMD_POD            (1u << 2)   /* Power On Device */
#define HBA_PxCMD_FRE            (1u << 4)   /* FIS Receive Enable */
#define HBA_PxCMD_FR             (1u << 14)  /* FIS Receive Running */
#define HBA_PxCMD_CR             (1u << 15)  /* Command List Running */

#define HBA_PxIS_TFES            (1u << 30)  /* Task File Error Status */

#define ATA_CMD_READ_DMA_EXT     0x25u
#define ATA_CMD_WRITE_DMA_EXT    0x35u
#define ATA_CMD_READ_FPDMA_Q     0x60u       /* NCQ Read */
#define ATA_CMD_WRITE_FPDMA_Q    0x61u       /* NCQ Write */
#define ATA_CMD_IDENTIFY         0xECu
#define ATA_CMD_IDENTIFY_PACKET  0xA1u
#define ATA_CMD_FLUSH_CACHE_EXT  0xEAu

/* FIS Types */
typedef enum {
    FIS_TYPE_REG_H2D   = 0x27,
    FIS_TYPE_REG_D2H   = 0x34,
    FIS_TYPE_DMA_ACT   = 0x39,
    FIS_TYPE_DMA_SETUP = 0x41,
    FIS_TYPE_DATA      = 0x46,
    FIS_TYPE_BIST      = 0x58,
    FIS_TYPE_PIO_SETUP = 0x5F,
    FIS_TYPE_DEV_BITS  = 0xA1,
} fis_type_t;

/* FIS: Register Host to Device (20 Bytes) */
typedef struct {
    u8  fis_type;             /* FIS_TYPE_REG_H2D */
    u8  pmport_c;             /* Port multiplier:4, Reserved:3, Command/Control:1 */
    u8  command;              /* Command register */
    u8  feature_low;          /* Feature register (7:0) */

    u8  lba0;                 /* LBA (7:0) */
    u8  lba1;                 /* LBA (15:8) */
    u8  lba2;                 /* LBA (23:16) */
    u8  device;               /* Device register */

    u8  lba3;                 /* LBA (31:24) */
    u8  lba4;                 /* LBA (39:32) */
    u8  lba5;                 /* LBA (47:40) */
    u8  feature_high;         /* Feature register (15:8) */

    u8  count_low;            /* Count (7:0) */
    u8  count_high;           /* Count (15:8) */
    u8  icc;                  /* Isochronous command completion */
    u8  control;              /* Control register */

    u8  reserved[4];
} __attribute__((packed)) fis_reg_h2d_t;

/* FIS: Register Device to Host (20 Bytes) */
typedef struct {
    u8  fis_type;             /* FIS_TYPE_REG_D2H */
    u8  pmport_i;             /* Port multiplier:4, Reserved:2, Interrupt:1, Reserved:1 */
    u8  status;               /* Status register */
    u8  error;                /* Error register */

    u8  lba0;
    u8  lba1;
    u8  lba2;
    u8  device;

    u8  lba3;
    u8  lba4;
    u8  lba5;
    u8  reserved1;

    u8  count_low;
    u8  count_high;
    u8  reserved2[2];

    u8  reserved3[4];
} __attribute__((packed)) fis_reg_d2h_t;

/* Physical Region Descriptor Table Entry (PRDT - 16 Bytes) */
typedef struct {
    u32 data_base_addr_low;
    u32 data_base_addr_high;
    u32 reserved;
    u32 byte_count_and_i;     /* Byte count:22 (bit 0=1 for 2 bytes, up to 4MB), Bit 31: Interrupt on Completion */
} __attribute__((packed)) ahci_prdt_entry_t;

/* Command Table Structure (CFIS + ACMD + PRDT Array) */
typedef struct {
    u8  cfis[64];             /* Command FIS */
    u8  acmd[16];             /* ATAPI Command */
    u8  reserved[48];
    ahci_prdt_entry_t prdt[AHCI_MAX_PRDT_ENTRIES];
} __attribute__((packed)) ahci_cmd_table_t;

/* 32-Byte Command List Header */
typedef struct {
    u8  cfl_and_flags;        /* CFL:5, A:1, W:1, P:1 */
    u8  flags2;               /* R:1, B:1, C:1, Rsv:1, PMP:4 */
    u16 prdtl;                /* PRDT length in entries */
    u32 prdbc;                /* PRD byte count transferred */
    u32 ctba;                 /* Command table base address low */
    u32 ctbau;                /* Command table base address high */
    u32 reserved[4];
} __attribute__((packed)) ahci_cmd_header_t;

/* Port Memory Register Map (128 Bytes per port) */
typedef volatile struct {
    u32 clb;                  /* Command list base address low */
    u32 clbu;                 /* Command list base address high */
    u32 fb;                   /* FIS base address low */
    u32 fbu;                  /* FIS base address high */
    u32 is;                   /* Interrupt status */
    u32 ie;                   /* Interrupt enable */
    u32 cmd;                  /* Command and status */
    u32 reserved0;
    u32 tfd;                  /* Task file data */
    u32 sig;                  /* Signature */
    u32 ssts;                 /* SATA status (SCR0:SStatus) */
    u32 sctl;                 /* SATA control (SCR2:SControl) */
    u32 serr;                 /* SATA error (SCR1:SError) */
    u32 sact;                 /* SATA active (SCR3:SActive) */
    u32 ci;                   /* Command issue */
    u32 sntf;                 /* SATA notification */
    u32 fbs;                  /* FIS-based switching control */
    u32 devslp;               /* Device sleep */
    u32 reserved1[10];
    u32 vendor[4];
} __attribute__((packed)) ahci_port_t;

/* Generic Host Control (HBA Memory Registers) */
typedef volatile struct {
    u32 cap;                  /* Host capability */
    u32 ghc;                  /* Global host control */
    u32 is;                   /* Interrupt status */
    u32 pi;                   /* Ports implemented (bitmask) */
    u32 vs;                   /* Version */
    u32 ccc_ctl;              /* Command completion coalescing control */
    u32 ccc_pts;              /* Command completion coalescing ports */
    u32 em_loc;               /* Enclosure management location */
    u32 em_ctl;               /* Enclosure management control */
    u32 cap2;                 /* Host capability extended */
    u32 bohc;                 /* BIOS/OS handoff control and status */
    u8  reserved[0x74];
    u8  vendor[0x60];
    ahci_port_t ports[AHCI_MAX_PORTS];
} __attribute__((packed)) ahci_hba_mem_t;

/* In-Memory SATA Device State */
typedef struct {
    u8   is_present;
    u8   port_idx;
    u8   is_atapi;
    u8   is_ssd;
    u64  sector_count;
    u32  sector_size;
    char model[41];
    char serial[21];
    char firmware[9];
    ahci_port_t* port_mmio;
    ahci_cmd_header_t* cmd_headers;
    ahci_cmd_table_t*  cmd_tables;
    u8*                received_fis;
} ahci_device_t;

/* Public API */
int  ahci_init(void);
int  ahci_probe_ports(ahci_hba_mem_t* hba);
int  ahci_identify_device(ahci_device_t* dev);
int  ahci_read_sectors(ahci_device_t* dev, u64 lba, u32 count, u8* out_buf);
int  ahci_write_sectors(ahci_device_t* dev, u64 lba, u32 count, const u8* in_buf);
int  ahci_port_start(ahci_port_t* port);
int  ahci_port_stop(ahci_port_t* port);
void ahci_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_BLOCK_AHCI_H */
