#ifndef NVME_H
#define NVME_H

#include "types.h"

int nvme_init(void);
int nvme_ready(void);
u32 nvme_sector_count(void);
int nvme_read_sector(u32 lba, u8* out512);
int nvme_write_sector(u32 lba, const u8* in512);

#endif
