#ifndef DRIVERS_BLOCK_IDE_H
#define DRIVERS_BLOCK_IDE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IDE_PRIMARY_IO_BASE     0x1F0u
#define IDE_PRIMARY_CTRL_BASE   0x3F6u
#define IDE_SECONDARY_IO_BASE   0x170u
#define IDE_SECONDARY_CTRL_BASE 0x376u

#define IDE_SECTOR_SIZE         512u
#define IDE_MAX_DRIVES          4u

/* ATA Status Register Bits */
#define ATA_SR_ERR              0x01u /* Error */
#define ATA_SR_INDEX            0x02u /* Index */
#define ATA_SR_CORR             0x04u /* Corrected data */
#define ATA_SR_DRQ              0x08u /* Data Request Ready */
#define ATA_SR_DSC              0x10u /* Drive Seek Complete */
#define ATA_SR_DF               0x20u /* Drive Fault Error */
#define ATA_SR_DRDY             0x40u /* Drive Ready */
#define ATA_SR_BSY              0x80u /* Drive Busy */

/* ATA Commands */
#define ATA_CMD_READ_PIO        0x20u
#define ATA_CMD_READ_PIO_EXT    0x24u
#define ATA_CMD_WRITE_PIO       0x30u
#define ATA_CMD_WRITE_PIO_EXT   0x34u
#define ATA_CMD_READ_DMA        0xC8u
#define ATA_CMD_WRITE_DMA       0xCAu
#define ATA_CMD_IDENTIFY        0xECu
#define ATA_CMD_IDENTIFY_PACKET 0xA1u
#define ATA_CMD_FLUSH_CACHE     0xE7u

/* In-Memory IDE Drive State */
typedef struct {
    u8   is_present;
    u8   is_atapi;
    u8   channel;             /* 0 = Primary, 1 = Secondary */
    u8   drive_num;           /* 0 = Master, 1 = Slave */
    u16  io_base;             /* 0x1F0 or 0x170 */
    u16  ctrl_base;           /* 0x3F6 or 0x376 */
    u64  sector_count;        /* Total sectors */
    char model[41];
    char serial[21];
    char firmware[9];
} ide_drive_t;

/* Public API */
void ide_init(void);
int  ide_available(void);
int  ide_read_sector(u8 drive, u32 lba, u8* out_buf);
int  ide_write_sector(u8 drive, u32 lba, const u8* in_buf);
int  ide_read_sectors_ext(u8 drive, u64 lba, u16 count, u8* out_buf);
int  ide_write_sectors_ext(u8 drive, u64 lba, u16 count, const u8* in_buf);
int  ide_identify(u8 drive, ide_drive_t* out_info);
void ide_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_BLOCK_IDE_H */
