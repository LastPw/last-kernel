#ifndef AHCI_H
#define AHCI_H

#include "types.h"

#define AHCI_BASE_MMIO      0xFEC00000ULL
#define AHCI_MAX_PORTS      32

#define AHCI_DEV_NULL       0
#define AHCI_DEV_SATA       1
#define AHCI_DEV_SATAPI     2
#define AHCI_DEV_SEMB       3
#define AHCI_DEV_PM         4

typedef struct {
    u32 clb;
    u32 clbu;
    u32 fb;
    u32 fbu;
    u32 is;
    u32 ie;
    u32 cmd;
    u32 reserved0;
    u32 tfd;
    u32 sig;
    u32 ssts;
    u32 sctl;
    u32 serr;
    u32 sact;
    u32 ci;
    u32 sntf;
    u32 fbs;
    u32 reserved1[11];
    u32 vendor[4];
} __attribute__((packed)) ahci_port_t;

typedef struct {
    u32 cap;
    u32 ghc;
    u32 is;
    u32 pi;
    u32 vs;
    u32 ccc_ctl;
    u32 ccc_pts;
    u32 em_loc;
    u32 em_ctl;
    u32 cap2;
    u32 bohc;
    u8  reserved[0xA0 - 0x2C];
    u8  vendor[0x100 - 0xA0];
    ahci_port_t ports[AHCI_MAX_PORTS];
} __attribute__((packed)) ahci_hba_t;

void ahci_init(void);
int ahci_available(void);
int ahci_read_sector(u32 port_no, u64 lba, u8* out_buf);
int ahci_write_sector(u32 port_no, u64 lba, const u8* in_buf);

#endif /* AHCI_H */
