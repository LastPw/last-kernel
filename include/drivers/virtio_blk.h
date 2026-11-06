#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "types.h"

int virtio_blk_init(void);
int virtio_blk_ready(void);
u32 virtio_blk_sector_count(void);
int virtio_blk_read_sector(u32 lba, u8* out512);
int virtio_blk_write_sector(u32 lba, const u8* in512);

#endif
