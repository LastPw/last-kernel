#ifndef DISK_H
#define DISK_H

#include "types.h"

#define DISK_SECTOR_SIZE 512u
#define DISK_MAX_DEVICES 16u
/* MBR supports up to 4 primary partitions; GPT supports up to 128.
   We allocate the full GPT capacity so the parser never overflows. */
#define DISK_MAX_PARTITIONS 128u
#define DISK_MAX_MBR_PARTS 4u

typedef enum {
    DISK_PART_MBR = 0,
    DISK_PART_GPT = 1
} disk_part_table_t;

typedef enum {
    DISK_FS_LASTFS = 0,
    DISK_FS_EXT4 = 1,
    DISK_FS_NTFS_RO = 2
} disk_fs_t;

typedef struct {
    u8 present;
    u8 writable;
    u8 bootable;
    u8 active_partitions;
    u32 id;
    u32 sector_size;
    u32 total_sectors_low;
    u32 total_sectors_high;
    u32 health_score;
    disk_part_table_t last_partition_table;
    u32 last_checksum;
    u8 rollback_ready;
    u8 dualboot_detected;
    u8 persistent;
    u8 backend;
} disk_info_t;

typedef struct {
    u8 valid;
    u8 index;
    u8 bootable;
    u8 reserved;
    disk_part_table_t table;
    disk_fs_t fs;
    u32 start_lba;
    u32 total_lba;
} disk_partition_info_t;

typedef struct {
    u32 total_sectors;
    u32 used_sectors;
    u32 free_sectors;
    u32 largest_free_extent;
    u32 free_extent_count;
} disk_free_space_t;

void disk_init(void);
u32 disk_count(void);
int disk_info(u32 index, disk_info_t* out);
int disk_select(u32 index);
int disk_read_sector(u32 lba, u8* out512);
int disk_read_sector_physical(u32 lba, u8* out512);
int disk_write_sector(u32 lba, const u8* in512);
int disk_write_sector_physical(u32 lba, const u8* in512);
int disk_write_sector_fast(u32 lba, const u8* in512);
int disk_flush(void);
int disk_copy_sectors(u32 src_lba, u32 dst_lba, u32 count);
u32 disk_checksum_range(u32 start_lba, u32 count);
int disk_snapshot_begin(void);
void disk_snapshot_discard(void);
int disk_snapshot_rollback(void);
int disk_detect_other_os(void);
int disk_partition_auto(disk_part_table_t table, disk_fs_t fs, u8 dualboot);
int disk_partition_info(disk_partition_info_t* out);
u32 disk_partition_count(void);
int disk_partition_get(u32 index, disk_partition_info_t* out);
int disk_free_space(disk_free_space_t* out);

#endif
