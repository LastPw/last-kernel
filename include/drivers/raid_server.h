#ifndef RAID_SERVER_H
#define RAID_SERVER_H

#include "types.h"

#define RAID_SERVER_MAX_VOLUMES     8
#define RAID_SERVER_MAX_DISKS       16
#define RAID_CHUNK_SIZE_DEFAULT     65536  /* 64 KB Stripe Chunk */

typedef enum {
    RAID_SRV_LEVEL_0    = 0,   /* Stripe */
    RAID_SRV_LEVEL_1    = 1,   /* Mirror */
    RAID_SRV_LEVEL_5    = 5,   /* Distributed Parity (Single Failure Tolerance) */
    RAID_SRV_LEVEL_6    = 6,   /* Dual Parity Galois Field P+Q (Dual Failure Tolerance) */
    RAID_SRV_LEVEL_10   = 10   /* Striped Mirrors */
} raid_srv_level_t;

typedef enum {
    RAID_STATE_OPTIMAL      = 0,
    RAID_STATE_DEGRADED     = 1,
    RAID_STATE_REBUILDING   = 2,
    RAID_STATE_FAILED       = 3
} raid_state_t;

typedef struct {
    u32 disk_id;
    u8  is_online;
    u8  is_spare;
    u64 total_sectors;
    u64 bad_sectors_count;
} raid_member_disk_t;

typedef struct {
    u32                 volume_id;
    u8                  active;
    raid_srv_level_t    level;
    raid_state_t        state;
    u32                 num_disks;
    u32                 chunk_size;
    u64                 total_capacity_bytes;
    u64                 usable_capacity_bytes;
    raid_member_disk_t  disks[RAID_SERVER_MAX_DISKS];
    u32                 hot_spare_count;
    u32                 rebuild_progress_percent;
    u64                 reads_completed;
    u64                 writes_completed;
    u64                 reconstructed_blocks;
} raid_server_volume_t;

/* Server RAID Subsystem API */
void raid_server_init(void);
int raid_server_create_volume(u32 volume_id, raid_srv_level_t level, u32 num_disks, const u32* disk_ids, u32 chunk_size);
int raid_server_add_hot_spare(u32 volume_id, u32 spare_disk_id);
int raid_server_simulate_disk_failure(u32 volume_id, u32 failed_disk_index);
int raid_server_rebuild(u32 volume_id);

int raid_server_read(u32 volume_id, u64 lba_offset, u32 num_sectors, u8* buffer);
int raid_server_write(u32 volume_id, u64 lba_offset, u32 num_sectors, const u8* buffer);

/* Galois Field GF(2^8) Matrix Operations for RAID 6 Dual-Parity */
u8 gf28_mul(u8 a, u8 b);
u8 gf28_inv(u8 a);
void raid6_calc_syndrome(const u8** data_blocks, u32 num_data, u32 block_size, u8* p_out, u8* q_out);
int raid6_recover_2disks(const u8** surviving_blocks, u32 num_data, u32 block_size,
                         u32 failed_idx1, u32 failed_idx2,
                         u8* rec_out1, u8* rec_out2);

int raid_server_get_volume_info(u32 volume_id, raid_server_volume_t* out);

#endif /* RAID_SERVER_H */
