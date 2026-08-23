#include "raid.h"
#include "disk.h"
#include "string.h"
#include "serial.h"

static raid_volume_t g_volumes[RAID_MAX_VOLUMES];
static u32 g_volume_count = 0u;
static i32 g_cur_volume = -1;

int raid_init(void) {
    memset(g_volumes, 0, sizeof(g_volumes));
    g_volume_count = 0u;
    g_cur_volume = -1;
    serial_writeln("[LastOS] RAID subsystem initialized");
    return 0;
}

int raid_create(u8 level, u8 num_disks, const u32* disk_ids, u8 chunk_shift) {
    if (level != RAID_LEVEL_0 && level != RAID_LEVEL_1) return -1;
    if (num_disks < 2u || num_disks > RAID_MAX_DISKS) return -1;
    if (!disk_ids) return -1;
    if (g_volume_count >= RAID_MAX_VOLUMES) return -1;

    u32 vol = g_volume_count;

    /* Verify all member disks exist and have the same size */
    disk_info_t info[RAID_MAX_DISKS];
    u32 min_sectors = 0xFFFFFFFFu;
    for (u32 i = 0u; i < num_disks; i++) {
        if (disk_info(disk_ids[i], &info[i]) != 0 || !info[i].present) {
            serial_write("[LastOS] RAID: disk "); serial_write_dec(disk_ids[i]);
            serial_writeln(" not found");
            return -2;
        }
        if (info[i].total_sectors_low < min_sectors) min_sectors = info[i].total_sectors_low;
        if (level == RAID_LEVEL_0 && info[i].total_sectors_low != info[0].total_sectors_low) {
            serial_writeln("[LastOS] RAID0: disks must be same size");
            return -3;
        }
    }

    memset(&g_volumes[vol], 0, sizeof(raid_volume_t));
    g_volumes[vol].active = 1u;
    g_volumes[vol].level = level;
    g_volumes[vol].num_disks = num_disks;
    g_volumes[vol].chunk_shift = chunk_shift ? chunk_shift : 3u; /* default 4KB chunks */
    for (u32 i = 0u; i < num_disks; i++) g_volumes[vol].disk_ids[i] = disk_ids[i];

    if (level == RAID_LEVEL_1) {
        /* Mirror: total = smallest disk */
        g_volumes[vol].total_sectors = min_sectors;
    } else {
        /* Striping: total = sum of all disks (rounded down to stripe boundary) */
        u64 total = 0u;
        for (u32 i = 0u; i < num_disks; i++) total += info[i].total_sectors_low;
        u32 chunk_sectors = 1u << chunk_shift;
        u32 stripe_sectors = chunk_sectors * num_disks;
        g_volumes[vol].stripe_sectors = stripe_sectors;
        g_volumes[vol].total_sectors = (u32)((total / stripe_sectors) * stripe_sectors);
    }

    g_volumes[vol].num_disks = num_disks;
    g_volume_count++;

    serial_write("[LastOS] RAID"); serial_write_dec(level);
    serial_write(" volume created: "); serial_write_dec(num_disks);
    serial_write(" disks, "); serial_write_dec(g_volumes[vol].total_sectors);
    serial_writeln(" sectors");
    return (int)vol;
}

int raid_select(u32 volume_index) {
    if (volume_index >= g_volume_count || !g_volumes[volume_index].active) return -1;
    g_cur_volume = (i32)volume_index;
    return 0;
}

u32 raid_volume_count(void) {
    return g_volume_count;
}

int raid_info(u32 index, raid_volume_t* out) {
    if (!out || index >= g_volume_count || !g_volumes[index].active) return -1;
    *out = g_volumes[index];
    return 0;
}

static int raid1_read(u32 lba, u8* out512) {
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    /* Read from first available member */
    for (u32 i = 0u; i < vol->num_disks; i++) {
        if (disk_select(vol->disk_ids[i]) == 0) {
            if (disk_read_sector(lba, out512) == 0) return 0;
        }
    }
    return -1;
}

static int raid1_write(u32 lba, const u8* in512) {
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    int ok_count = 0;
    int members = 0;
    for (u32 i = 0u; i < vol->num_disks; i++) {
        if (disk_select(vol->disk_ids[i]) != 0) continue;
        members++;
        if (disk_write_sector(lba, in512) == 0) {
            ok_count++;
        }
    }
    /* A mirror write succeeds only when at least one member accepted it.
       Members that failed are now out of sync; callers should re-sync the
       degraded volume, but we do not silently report success for a mirror
       with no good copies. */
    (void)members;
    return (ok_count > 0) ? 0 : -1;
}

static int raid0_read(u32 lba, u8* out512) {
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    u32 chunk_sectors = 1u << vol->chunk_shift;
    u32 stripe_sectors = vol->stripe_sectors;

    u32 stripe_index = lba / stripe_sectors;
    u32 stripe_offset = lba % stripe_sectors;
    u32 disk_index = stripe_offset / chunk_sectors;
    u32 disk_lba = stripe_index * chunk_sectors + (stripe_offset % chunk_sectors);

    if (disk_index >= vol->num_disks) return -1;
    if (disk_select(vol->disk_ids[disk_index]) != 0) return -1;
    return disk_read_sector(disk_lba, out512);
}

static int raid0_write(u32 lba, const u8* in512) {
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    u32 chunk_sectors = 1u << vol->chunk_shift;
    u32 stripe_sectors = vol->stripe_sectors;

    u32 stripe_index = lba / stripe_sectors;
    u32 stripe_offset = lba % stripe_sectors;
    u32 disk_index = stripe_offset / chunk_sectors;
    u32 disk_lba = stripe_index * chunk_sectors + (stripe_offset % chunk_sectors);

    if (disk_index >= vol->num_disks) return -1;
    if (disk_select(vol->disk_ids[disk_index]) != 0) return -1;
    return disk_write_sector(disk_lba, in512);
}

int raid_read_sector(u32 lba, u8* out512) {
    if (g_cur_volume < 0 || !out512) return -1;
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    if (lba >= vol->total_sectors) return -1;

    if (vol->level == RAID_LEVEL_1) return raid1_read(lba, out512);
    if (vol->level == RAID_LEVEL_0) return raid0_read(lba, out512);
    return -1;
}

int raid_write_sector(u32 lba, const u8* in512) {
    if (g_cur_volume < 0 || !in512) return -1;
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    if (lba >= vol->total_sectors) return -1;

    if (vol->level == RAID_LEVEL_1) return raid1_write(lba, in512);
    if (vol->level == RAID_LEVEL_0) return raid0_write(lba, in512);
    return -1;
}

int raid_flush(void) {
    if (g_cur_volume < 0) return -1;
    raid_volume_t* vol = &g_volumes[(u32)g_cur_volume];
    for (u32 i = 0u; i < vol->num_disks; i++) {
        if (disk_select(vol->disk_ids[i]) == 0) disk_flush();
    }
    return 0;
}
