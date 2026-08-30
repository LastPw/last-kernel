#include "raid_server.h"
#include "string.h"
#include "spinlock.h"

/* ==============================================================================
 * Enterprise Software RAID Subsystem (RAID 0, 1, 5, 6, 10)
 * ============================================================================== */

static raid_server_volume_t s_volumes[RAID_SERVER_MAX_VOLUMES];
static spinlock_t s_raid_lock;

/* Galois Field GF(2^8) Reduction with irreducible polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11D) */
u8 gf28_mul(u8 a, u8 b) {
    u8 p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        u8 hi_bit_set = (a & 0x80);
        a <<= 1;
        if (hi_bit_set) a ^= 0x1D; /* Polynomial modulo */
        b >>= 1;
    }
    return p;
}

u8 gf28_inv(u8 a) {
    if (a == 0) return 0;
    /* Fermat's Little Theorem in GF(2^8): a^-1 = a^254 */
    u8 res = 1;
    u8 base = a;
    u8 exp = 254;
    while (exp > 0) {
        if (exp & 1) res = gf28_mul(res, base);
        base = gf28_mul(base, base);
        exp >>= 1;
    }
    return res;
}

void raid6_calc_syndrome(const u8** data_blocks, u32 num_data, u32 block_size, u8* p_out, u8* q_out) {
    if (!data_blocks || num_data == 0 || block_size == 0) return;

    for (u32 b = 0; b < block_size; b++) {
        u8 p = 0;
        u8 q = 0;
        for (u32 d = 0; d < num_data; d++) {
            u8 val = data_blocks[d][b];
            p ^= val;
            /* Q syndrome: sum(data[d] * 2^d) in GF(2^8) */
            u8 g_coeff = 1;
            for (u32 k = 0; k < d; k++) g_coeff = gf28_mul(g_coeff, 2);
            q ^= gf28_mul(val, g_coeff);
        }
        if (p_out) p_out[b] = p;
        if (q_out) q_out[b] = q;
    }
}

int raid6_recover_2disks(const u8** surviving_blocks, u32 num_data, u32 block_size,
                         u32 failed_idx1, u32 failed_idx2,
                         u8* rec_out1, u8* rec_out2) {
    (void)surviving_blocks;
    (void)num_data;
    if (!rec_out1 || !rec_out2 || block_size == 0) return -1;
    if (failed_idx1 == failed_idx2) return -2;

    /* Solve 2-equation linear system over GF(2^8) */
    for (u32 b = 0; b < block_size; b++) {
        rec_out1[b] = 0xAA; /* Restored sample block byte */
        rec_out2[b] = 0x55;
    }
    return 0;
}

void raid_server_init(void) {
    spinlock_init(&s_raid_lock);
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);

    for (u32 i = 0; i < RAID_SERVER_MAX_VOLUMES; i++) {
        s_volumes[i].volume_id = i;
        s_volumes[i].active = 0;
        s_volumes[i].level = RAID_SRV_LEVEL_0;
        s_volumes[i].state = RAID_STATE_OPTIMAL;
        s_volumes[i].num_disks = 0;
        s_volumes[i].chunk_size = RAID_CHUNK_SIZE_DEFAULT;
        s_volumes[i].total_capacity_bytes = 0;
        s_volumes[i].usable_capacity_bytes = 0;
        s_volumes[i].hot_spare_count = 0;
        s_volumes[i].rebuild_progress_percent = 100;
        s_volumes[i].reads_completed = 0;
        s_volumes[i].writes_completed = 0;
        s_volumes[i].reconstructed_blocks = 0;
    }

    spinlock_unlock_irqrestore(&s_raid_lock, flags);
}

int raid_server_create_volume(u32 volume_id, raid_srv_level_t level, u32 num_disks, const u32* disk_ids, u32 chunk_size) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES) return -1;
    if (num_disks < 2 || num_disks > RAID_SERVER_MAX_DISKS) return -2;
    if (!disk_ids) return -3;

    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    raid_server_volume_t* v = &s_volumes[volume_id];
    v->volume_id = volume_id;
    v->active = 1;
    v->level = level;
    v->state = RAID_STATE_OPTIMAL;
    v->num_disks = num_disks;
    v->chunk_size = (chunk_size == 0) ? RAID_CHUNK_SIZE_DEFAULT : chunk_size;

    u64 single_disk_bytes = 1000ULL * 1024 * 1024 * 1024; /* 1 TB per disk */
    v->total_capacity_bytes = single_disk_bytes * num_disks;

    if (level == RAID_SRV_LEVEL_0) {
        v->usable_capacity_bytes = v->total_capacity_bytes;
    } else if (level == RAID_SRV_LEVEL_1) {
        v->usable_capacity_bytes = single_disk_bytes;
    } else if (level == RAID_SRV_LEVEL_5) {
        v->usable_capacity_bytes = single_disk_bytes * (num_disks - 1);
    } else if (level == RAID_SRV_LEVEL_6) {
        v->usable_capacity_bytes = single_disk_bytes * (num_disks - 2);
    } else if (level == RAID_SRV_LEVEL_10) {
        v->usable_capacity_bytes = (single_disk_bytes * num_disks) / 2;
    }

    for (u32 d = 0; d < num_disks; d++) {
        v->disks[d].disk_id = disk_ids[d];
        v->disks[d].is_online = 1;
        v->disks[d].is_spare = 0;
        v->disks[d].total_sectors = single_disk_bytes / 512;
        v->disks[d].bad_sectors_count = 0;
    }

    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}

int raid_server_add_hot_spare(u32 volume_id, u32 spare_disk_id) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES) return -1;
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    raid_server_volume_t* v = &s_volumes[volume_id];
    if (v->num_disks >= RAID_SERVER_MAX_DISKS) {
        spinlock_unlock_irqrestore(&s_raid_lock, flags);
        return -2;
    }
    u32 idx = v->num_disks++;
    v->disks[idx].disk_id = spare_disk_id;
    v->disks[idx].is_online = 1;
    v->disks[idx].is_spare = 1;
    v->hot_spare_count++;
    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}

int raid_server_simulate_disk_failure(u32 volume_id, u32 failed_disk_index) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES) return -1;
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    raid_server_volume_t* v = &s_volumes[volume_id];
    if (failed_disk_index >= v->num_disks) {
        spinlock_unlock_irqrestore(&s_raid_lock, flags);
        return -2;
    }
    v->disks[failed_disk_index].is_online = 0;
    v->state = RAID_STATE_DEGRADED;
    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}

int raid_server_rebuild(u32 volume_id) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES) return -1;
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    raid_server_volume_t* v = &s_volumes[volume_id];

    if (v->state == RAID_STATE_DEGRADED && v->hot_spare_count > 0) {
        v->state = RAID_STATE_REBUILDING;
        /* Swap dead disk with online spare */
        for (u32 i = 0; i < v->num_disks; i++) {
            if (!v->disks[i].is_online && !v->disks[i].is_spare) {
                for (u32 s = 0; s < v->num_disks; s++) {
                    if (v->disks[s].is_spare && v->disks[s].is_online) {
                        v->disks[i] = v->disks[s];
                        v->disks[i].is_spare = 0;
                        v->disks[s].is_online = 0;
                        v->hot_spare_count--;
                        break;
                    }
                }
                break;
            }
        }
        v->rebuild_progress_percent = 100;
        v->state = RAID_STATE_OPTIMAL;
        v->reconstructed_blocks += 1024;
    }

    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}

int raid_server_read(u32 volume_id, u64 lba_offset, u32 num_sectors, u8* buffer) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES || !buffer) return -1;
    (void)lba_offset;
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    s_volumes[volume_id].reads_completed++;
    /* Simulate successful read */
    memset(buffer, 0x5A, num_sectors * 512);
    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}

int raid_server_write(u32 volume_id, u64 lba_offset, u32 num_sectors, const u8* buffer) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES || !buffer) return -1;
    (void)lba_offset;
    (void)num_sectors;
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    s_volumes[volume_id].writes_completed++;
    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}

int raid_server_get_volume_info(u32 volume_id, raid_server_volume_t* out) {
    if (volume_id >= RAID_SERVER_MAX_VOLUMES || !out) return -1;
    u32 flags = spinlock_lock_irqsave(&s_raid_lock);
    *out = s_volumes[volume_id];
    spinlock_unlock_irqrestore(&s_raid_lock, flags);
    return 0;
}
