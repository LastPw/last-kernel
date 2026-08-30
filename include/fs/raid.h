#ifndef RAID_H
#define RAID_H

#include "types.h"
#include "disk.h"

#define RAID_MAX_DISKS 4u
#define RAID_MAX_VOLUMES 2u
#define RAID_NAME_MAX 16u

typedef enum {
    RAID_LEVEL_NONE = 0,
    RAID_LEVEL_0 = 1,   /* Striping */
    RAID_LEVEL_1 = 2    /* Mirroring */
} raid_level_t;

typedef struct {
    u8  active;
    u8  level;           /* raid_level_t */
    u8  num_disks;
    u8  chunk_shift;     /* log2(chunk_size / 512), default 3 = 4KB chunks */
    u32 disk_ids[RAID_MAX_DISKS];
    u32 total_sectors;
    u32 stripe_sectors;  /* sectors per stripe (num_disks * chunk_size/512) */
    char name[RAID_NAME_MAX];
} raid_volume_t;

/* RAID API */
int raid_init(void);
int raid_create(u8 level, u8 num_disks, const u32* disk_ids, u8 chunk_shift);
int raid_select(u32 volume_index);
u32 raid_volume_count(void);
int raid_info(u32 index, raid_volume_t* out);

/* I/O through the RAID volume (calls disk_read_sector/disk_write_sector on members) */
int raid_read_sector(u32 lba, u8* out512);
int raid_write_sector(u32 lba, const u8* in512);
int raid_flush(void);

#endif
