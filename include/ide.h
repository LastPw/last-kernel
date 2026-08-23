#ifndef IDE_H
#define IDE_H

#include "types.h"

#define IDE_PRIMARY_IO_BASE    0x1F0u
#define IDE_SECONDARY_IO_BASE  0x170u
#define IDE_SECTOR_SIZE        512u

void ide_init(void);
int  ide_available(void);
int  ide_read_sector(u8 drive, u32 lba, u8* out_buf);
int  ide_write_sector(u8 drive, u32 lba, const u8* in_buf);

#endif
