#include "lastfs.h"
#include "disk.h"
#include "string.h"

#define LASTFS_MAGIC 0x5346544Cu /* LTFS */
#define LASTFS_VERSION 1u
#define LASTFS_INODE_MAX 64u
#define LASTFS_DIR_MAX 64u

typedef struct {
    u32 magic;
    u32 version;
    u32 total_lba;
    u32 inode_lba;
    u32 inode_count;
    u32 dir_lba;
    u32 dir_count;
    u32 data_lba;
    u32 free_lba;
    u32 checksum;
} lastfs_super_t;

typedef struct {
    u32 id;
    u32 type; /*1=file,2=dir*/
    u32 size_bytes;
    u32 data_lba;
    u32 data_count;
    char name[32];
} lastfs_inode_t;

typedef struct {
    u32 inode_id;
    char name[32];
} lastfs_dirent_t;

static u32 fnv1a32(const u8* p, u32 n) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void wr32(u8* p, u32 v) {
    p[0] = (u8)(v & 0xFFu);
    p[1] = (u8)((v >> 8u) & 0xFFu);
    p[2] = (u8)((v >> 16u) & 0xFFu);
    p[3] = (u8)((v >> 24u) & 0xFFu);
}

static void zero_region(u32 start_lba, u32 count) {
    u8 sec[DISK_SECTOR_SIZE];
    memset(sec, 0, sizeof(sec));
    for (u32 i = 0; i < count; i++) {
        (void)disk_write_sector(start_lba + i, sec);
    }
}

int lastfs_format(const lastfs_seed_plan_t* plan) {
    if (!plan) return -1;
    if (plan->start_lba == 0u || plan->total_lba < 2048u) return -1;

    u32 super_lba = plan->start_lba;
    u32 inode_lba = super_lba + 1u;
    u32 inode_sectors = 16u;
    u32 dir_lba = inode_lba + inode_sectors;
    u32 dir_sectors = 8u;
    u32 data_lba = dir_lba + dir_sectors;
    u32 boot_dst = data_lba;
    u32 kernel_dst = boot_dst + plan->boot_src_count;

    if (kernel_dst + plan->kernel_src_count >= plan->start_lba + plan->total_lba) return -1;

    zero_region(super_lba, 64u);

    if (disk_copy_sectors(plan->boot_src_lba, boot_dst, plan->boot_src_count) != 0) return -1;
    if (disk_copy_sectors(plan->kernel_src_lba, kernel_dst, plan->kernel_src_count) != 0) return -1;

    lastfs_inode_t ino[LASTFS_INODE_MAX];
    memset(ino, 0, sizeof(ino));
    ino[0].id = 1u; ino[0].type = 2u; ino[0].size_bytes = 0u; ino[0].data_lba = dir_lba; ino[0].data_count = dir_sectors; str_ncpy(ino[0].name, "/", 31u);
    ino[1].id = 2u; ino[1].type = 1u; ino[1].size_bytes = plan->boot_src_count * DISK_SECTOR_SIZE; ino[1].data_lba = boot_dst; ino[1].data_count = plan->boot_src_count; str_ncpy(ino[1].name, "BOOT.BIN", 31u);
    ino[2].id = 3u; ino[2].type = 1u; ino[2].size_bytes = plan->kernel_src_count * DISK_SECTOR_SIZE; ino[2].data_lba = kernel_dst; ino[2].data_count = plan->kernel_src_count; str_ncpy(ino[2].name, "KERNEL.BIN", 31u);

    lastfs_dirent_t de[LASTFS_DIR_MAX];
    memset(de, 0, sizeof(de));
    de[0].inode_id = 2u; str_ncpy(de[0].name, "BOOT.BIN", 31u);
    de[1].inode_id = 3u; str_ncpy(de[1].name, "KERNEL.BIN", 31u);

    u8 sec[DISK_SECTOR_SIZE];
    u8* pbytes = (u8*)ino;
    u32 ino_bytes = sizeof(ino);
    for (u32 i = 0; i < inode_sectors; i++) {
        memset(sec, 0, sizeof(sec));
        u32 off = i * DISK_SECTOR_SIZE;
        if (off < ino_bytes) {
            u32 take = ino_bytes - off;
            if (take > DISK_SECTOR_SIZE) take = DISK_SECTOR_SIZE;
            memcpy(sec, pbytes + off, take);
        }
        if (disk_write_sector(inode_lba + i, sec) != 0) return -1;
    }

    pbytes = (u8*)de;
    u32 de_bytes = sizeof(de);
    for (u32 i = 0; i < dir_sectors; i++) {
        memset(sec, 0, sizeof(sec));
        u32 off = i * DISK_SECTOR_SIZE;
        if (off < de_bytes) {
            u32 take = de_bytes - off;
            if (take > DISK_SECTOR_SIZE) take = DISK_SECTOR_SIZE;
            memcpy(sec, pbytes + off, take);
        }
        if (disk_write_sector(dir_lba + i, sec) != 0) return -1;
    }

    lastfs_super_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = LASTFS_MAGIC;
    sb.version = LASTFS_VERSION;
    sb.total_lba = plan->total_lba;
    sb.inode_lba = inode_lba;
    sb.inode_count = LASTFS_INODE_MAX;
    sb.dir_lba = dir_lba;
    sb.dir_count = LASTFS_DIR_MAX;
    sb.data_lba = data_lba;
    sb.free_lba = kernel_dst + plan->kernel_src_count;
    sb.checksum = fnv1a32((const u8*)&sb, sizeof(sb) - sizeof(u32));

    memset(sec, 0, sizeof(sec));
    wr32(sec + 0u, sb.magic);
    wr32(sec + 4u, sb.version);
    wr32(sec + 8u, sb.total_lba);
    wr32(sec + 12u, sb.inode_lba);
    wr32(sec + 16u, sb.inode_count);
    wr32(sec + 20u, sb.dir_lba);
    wr32(sec + 24u, sb.dir_count);
    wr32(sec + 28u, sb.data_lba);
    wr32(sec + 32u, sb.free_lba);
    wr32(sec + 36u, sb.checksum);
    if (disk_write_sector(super_lba, sec) != 0) return -1;

    return 0;
}
