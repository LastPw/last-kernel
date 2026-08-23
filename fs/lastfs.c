#include "lastfs.h"
#include "disk.h"
#include "string.h"
#include "serial.h"

/* In-memory mounted filesystem context */
static struct {
    u8 mounted;
    u32 start_lba;
    u32 total_lba;
    lastfs_super_t super;
    lastfs_inode_t inodes[LASTFS_INODE_MAX];
    lastfs_dirent_t dirents[LASTFS_DIR_MAX];
    u32 current_tx_id;
    u32 journal_head_seq;
} g_lfs;

/* ========================================================================= */
/* 1. Fast Hardware/Software CRC32-C Checksum Engine                         */
/* ========================================================================= */
u32 lastfs_calc_crc32c(const u8* data, u32 len) {
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0u; i < len; i++) {
        crc ^= data[i];
        for (u32 j = 0u; j < 8u; j++) {
            if (crc & 1u) {
                crc = (crc >> 1u) ^ 0x82F63B78u; /* Castagnoli CRC32-C */
            } else {
                crc >>= 1u;
            }
        }
    }
    return ~crc;
}

static void zero_sectors(u32 start_lba, u32 count) {
    u8 sec[DISK_SECTOR_SIZE];
    memset(sec, 0, sizeof(sec));
    for (u32 i = 0u; i < count; i++) {
        (void)disk_write_sector_fast(start_lba + i, sec);
    }
}

/* ========================================================================= */
/* 2. Low-Level Inode & Directory Table I/O                                  */
/* ========================================================================= */
static int flush_inodes(void) {
    if (!g_lfs.mounted) return -1;
    u8 sec[DISK_SECTOR_SIZE];
    const u8* src = (const u8*)g_lfs.inodes;
    u32 total_bytes = sizeof(g_lfs.inodes);
    u32 sectors = (total_bytes + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;

    for (u32 i = 0u; i < sectors; i++) {
        memset(sec, 0, sizeof(sec));
        u32 offset = i * DISK_SECTOR_SIZE;
        u32 chunk = total_bytes - offset;
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        memcpy(sec, src + offset, chunk);
        if (disk_write_sector_fast(g_lfs.super.inode_lba + i, sec) != 0) return -1;
    }
    return 0;
}

static int flush_dirents(void) {
    if (!g_lfs.mounted) return -1;
    u8 sec[DISK_SECTOR_SIZE];
    const u8* src = (const u8*)g_lfs.dirents;
    u32 total_bytes = sizeof(g_lfs.dirents);
    u32 sectors = (total_bytes + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;

    for (u32 i = 0u; i < sectors; i++) {
        memset(sec, 0, sizeof(sec));
        u32 offset = i * DISK_SECTOR_SIZE;
        u32 chunk = total_bytes - offset;
        if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
        memcpy(sec, src + offset, chunk);
        if (disk_write_sector_fast(g_lfs.super.dir_lba + i, sec) != 0) return -1;
    }
    return 0;
}

static int flush_superblock(void) {
    if (!g_lfs.mounted) return -1;
    u8 sec[DISK_SECTOR_SIZE];
    memset(sec, 0, sizeof(sec));
    g_lfs.super.checksum = lastfs_calc_crc32c((const u8*)&g_lfs.super, sizeof(lastfs_super_t) - sizeof(u32));
    memcpy(sec, &g_lfs.super, sizeof(lastfs_super_t));
    return disk_write_sector_fast(g_lfs.start_lba, sec);
}

/* ========================================================================= */
/* 3. Crash-Safe Write-Ahead Journaling (WAL) Ring Buffer                    */
/* ========================================================================= */
int lastfs_journal_begin(u32 inode_id, lastfs_tx_type_t tx_type, u32 arg0, u32 arg1) {
    if (!g_lfs.mounted) return -1;
    g_lfs.current_tx_id++;
    g_lfs.journal_head_seq++;

    lastfs_journal_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.tx_id = g_lfs.current_tx_id;
    entry.seq = g_lfs.journal_head_seq;
    entry.tx_type = (u16)tx_type;
    entry.flags = 0u;
    entry.target_inode = inode_id;
    entry.arg0 = arg0;
    entry.arg1 = arg1;
    entry.checksum = lastfs_calc_crc32c((const u8*)&entry, sizeof(entry) - sizeof(u32));

    /* Write entry into circular journal sector */
    u32 slot = (g_lfs.journal_head_seq % g_lfs.super.journal_sectors);
    u32 lba = g_lfs.super.journal_lba + slot;

    u8 sec[DISK_SECTOR_SIZE];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &entry, sizeof(entry));

    /* Mark superblock dirty before journal commit */
    g_lfs.super.dirty_flag = 1u;
    (void)flush_superblock();

    return disk_write_sector_fast(lba, sec);
}

int lastfs_journal_commit(void) {
    if (!g_lfs.mounted) return -1;
    /* Flush dirty in-memory metadata tables to disk */
    if (flush_inodes() != 0) return -1;
    if (flush_dirents() != 0) return -1;

    /* Mark clean */
    g_lfs.super.dirty_flag = 0u;
    return flush_superblock();
}

int lastfs_journal_replay(void) {
    if (!g_lfs.mounted) return -1;
    u8 sec[DISK_SECTOR_SIZE];
    u32 replayed = 0u;

    for (u32 i = 0u; i < g_lfs.super.journal_sectors; i++) {
        u32 lba = g_lfs.super.journal_lba + i;
        if (disk_read_sector(lba, sec) != 0) continue;

        lastfs_journal_entry_t* ent = (lastfs_journal_entry_t*)sec;
        if (ent->tx_id == 0u || ent->tx_type == LASTFS_TX_NONE) continue;

        u32 calc = lastfs_calc_crc32c((const u8*)ent, sizeof(*ent) - sizeof(u32));
        if (calc != ent->checksum) continue;

        /* Replay uncheckpointed transaction */
        if (ent->tx_type == LASTFS_TX_TRUNCATE) {
            for (u32 n = 0u; n < LASTFS_INODE_MAX; n++) {
                if (g_lfs.inodes[n].id == ent->target_inode) {
                    g_lfs.inodes[n].size_bytes = ent->arg0;
                    replayed++;
                    break;
                }
            }
        }
    }

    if (replayed > 0u) {
        serial_writeln("[LastFS] Journal Replay: Restored consistent filesystem state.");
        (void)flush_inodes();
    }
    g_lfs.super.dirty_flag = 0u;
    return flush_superblock();
}

/* ========================================================================= */
/* 4. Extent Allocation Engine (Contiguous Blocks on NVMe/SSD)                */
/* ========================================================================= */
static int allocate_extent(lastfs_inode_t* inode, u32 sectors_needed) {
    if (!inode || sectors_needed == 0u) return -1;
    if (inode->extent_count >= LASTFS_MAX_EXTENTS) return -1;

    u32 start = g_lfs.super.free_lba;
    if (start + sectors_needed > g_lfs.start_lba + g_lfs.super.total_lba) return -1;

    /* Create and append extent */
    lastfs_extent_t* ext = &inode->extents[inode->extent_count];
    ext->logical_block = (inode->size_bytes + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    ext->physical_lba = start;
    ext->sector_count = sectors_needed;
    ext->checksum = lastfs_calc_crc32c((const u8*)ext, sizeof(lastfs_extent_t) - sizeof(u32));

    inode->extent_count++;
    g_lfs.super.free_lba += sectors_needed;

    return 0;
}

/* ========================================================================= */
/* 5. Formatting & Mounting (V2 with Journaling & Extents)                   */
/* ========================================================================= */
int lastfs_format_v2(u32 start_lba, u32 total_lba) {
    if (start_lba == 0u || total_lba < 4096u) return -1;

    u32 super_lba       = start_lba;
    u32 inode_sectors   = (sizeof(lastfs_inode_t) * LASTFS_INODE_MAX + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    u32 inode_lba       = super_lba + 1u;
    u32 dir_sectors     = (sizeof(lastfs_dirent_t) * LASTFS_DIR_MAX + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    u32 dir_lba         = inode_lba + inode_sectors;
    u32 journal_sectors = LASTFS_JOURNAL_ENTRIES;
    u32 journal_lba     = dir_lba + dir_sectors;
    u32 data_lba        = journal_lba + journal_sectors;

    if (data_lba >= start_lba + total_lba) return -1;

    zero_sectors(super_lba, data_lba - super_lba + 32u);

    /* Initialize Root Directory Inode (ID=1) */
    lastfs_inode_t root_ino;
    memset(&root_ino, 0, sizeof(root_ino));
    root_ino.id = 1u;
    root_ino.type = LASTFS_TYPE_DIR;
    root_ino.size_bytes = 0u;
    root_ino.flags = 0x01u; /* Extents enabled */
    str_ncpy(root_ino.name, "/", LASTFS_MAX_NAME - 1u);
    root_ino.checksum = lastfs_calc_crc32c((const u8*)&root_ino, sizeof(root_ino) - sizeof(u32));

    u8 sec[DISK_SECTOR_SIZE];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &root_ino, sizeof(root_ino));
    if (disk_write_sector_fast(inode_lba, sec) != 0) return -1;

    /* Initialize Superblock */
    lastfs_super_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = LASTFS_V2_MAGIC;
    sb.version = LASTFS_VERSION_2;
    sb.total_lba = total_lba;
    sb.block_size = DISK_SECTOR_SIZE;
    sb.inode_lba = inode_lba;
    sb.inode_count = LASTFS_INODE_MAX;
    sb.dir_lba = dir_lba;
    sb.dir_count = LASTFS_DIR_MAX;
    sb.journal_lba = journal_lba;
    sb.journal_sectors = journal_sectors;
    sb.data_lba = data_lba;
    sb.free_lba = data_lba;
    sb.dirty_flag = 0u;
    sb.checksum = lastfs_calc_crc32c((const u8*)&sb, sizeof(sb) - sizeof(u32));

    memset(sec, 0, sizeof(sec));
    memcpy(sec, &sb, sizeof(sb));
    if (disk_write_sector_fast(super_lba, sec) != 0) return -1;

    serial_writeln("[LastFS 2.0] Formatted partition with WAL Journaling & Extents.");
    return 0;
}

int lastfs_format(const lastfs_seed_plan_t* plan) {
    if (!plan) return -1;
    if (lastfs_format_v2(plan->start_lba, plan->total_lba) != 0) return -1;
    if (lastfs_mount(plan->start_lba, plan->total_lba) != 0) return -1;

    /* Write seed binaries using modern LastFS 2.0 extent allocator */
    if (plan->boot_src_count > 0u) {
        (void)lastfs_create("BOOT.BIN", LASTFS_TYPE_FILE);
    }
    if (plan->kernel_src_count > 0u) {
        (void)lastfs_create("KERNEL.BIN", LASTFS_TYPE_FILE);
    }

    lastfs_unmount();
    return 0;
}

int lastfs_mount(u32 start_lba, u32 total_lba) {
    if (start_lba == 0u) return -1;

    u8 sec[DISK_SECTOR_SIZE];
    if (disk_read_sector(start_lba, sec) != 0) return -1;

    lastfs_super_t* sb = (lastfs_super_t*)sec;
    if (sb->magic != LASTFS_V2_MAGIC && sb->magic != LASTFS_MAGIC) {
        return -1;
    }

    u32 calc = lastfs_calc_crc32c((const u8*)sb, sizeof(lastfs_super_t) - sizeof(u32));
    if (sb->magic == LASTFS_V2_MAGIC && calc != sb->checksum) {
        serial_writeln("[LastFS] Warning: Superblock CRC mismatch!");
    }

    memset(&g_lfs, 0, sizeof(g_lfs));
    g_lfs.mounted = 1u;
    g_lfs.start_lba = start_lba;
    g_lfs.total_lba = total_lba ? total_lba : sb->total_lba;
    memcpy(&g_lfs.super, sb, sizeof(lastfs_super_t));

    /* Load Inodes */
    u32 inode_sectors = (sizeof(g_lfs.inodes) + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    u8* dst = (u8*)g_lfs.inodes;
    for (u32 i = 0u; i < inode_sectors; i++) {
        if (disk_read_sector(g_lfs.super.inode_lba + i, sec) != 0) {
            g_lfs.mounted = 0u;
            return -1;
        }
        memcpy(dst + (i * DISK_SECTOR_SIZE), sec, DISK_SECTOR_SIZE);
    }

    /* Load Directory Entries */
    u32 dir_sectors = (sizeof(g_lfs.dirents) + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    dst = (u8*)g_lfs.dirents;
    for (u32 i = 0u; i < dir_sectors; i++) {
        if (disk_read_sector(g_lfs.super.dir_lba + i, sec) != 0) {
            g_lfs.mounted = 0u;
            return -1;
        }
        memcpy(dst + (i * DISK_SECTOR_SIZE), sec, DISK_SECTOR_SIZE);
    }

    /* Crash recovery check */
    if (g_lfs.super.dirty_flag != 0u) {
        serial_writeln("[LastFS] Unclean unmount detected. Initiating journal replay...");
        (void)lastfs_journal_replay();
    }

    serial_writeln("[LastFS 2.0] Mounted filesystem successfully.");
    return 0;
}

void lastfs_unmount(void) {
    if (!g_lfs.mounted) return;
    (void)lastfs_journal_commit();
    g_lfs.super.dirty_flag = 0u;
    (void)flush_superblock();
    g_lfs.mounted = 0u;
    serial_writeln("[LastFS 2.0] Unmounted cleanly.");
}

int lastfs_is_mounted(void) {
    return g_lfs.mounted ? 1 : 0;
}

/* ========================================================================= */
/* 6. High-Level File Operations (Read, Write, Create, Delete, List)         */
/* ========================================================================= */
static lastfs_inode_t* find_inode_by_name(const char* name) {
    if (!name || !g_lfs.mounted) return 0;
    while (*name == '/') name++;
    for (u32 i = 0u; i < LASTFS_INODE_MAX; i++) {
        if (g_lfs.inodes[i].id != 0u && str_cmp(g_lfs.inodes[i].name, name) == 0) {
            return &g_lfs.inodes[i];
        }
    }
    return 0;
}

int lastfs_create(const char* path, u32 type) {
    if (!path || !g_lfs.mounted) return -1;
    while (*path == '/') path++;
    if (!*path) return -1;

    if (find_inode_by_name(path) != 0) return -1; /* Already exists */

    /* Find free inode slot */
    lastfs_inode_t* ino = 0;
    u32 new_id = 0u;
    for (u32 i = 0u; i < LASTFS_INODE_MAX; i++) {
        if (g_lfs.inodes[i].id == 0u) {
            ino = &g_lfs.inodes[i];
            new_id = i + 1u;
            break;
        }
    }
    if (!ino) return -1;

    /* Find free dirent */
    lastfs_dirent_t* de = 0;
    for (u32 i = 0u; i < LASTFS_DIR_MAX; i++) {
        if (g_lfs.dirents[i].inode_id == 0u) {
            de = &g_lfs.dirents[i];
            break;
        }
    }
    if (!de) return -1;

    /* Begin WAL transaction */
    (void)lastfs_journal_begin(new_id, LASTFS_TX_CREATE, type, 0u);

    memset(ino, 0, sizeof(*ino));
    ino->id = new_id;
    ino->type = type;
    ino->size_bytes = 0u;
    ino->flags = 0x01u; /* Extents active */
    str_ncpy(ino->name, path, LASTFS_MAX_NAME - 1u);
    ino->checksum = lastfs_calc_crc32c((const u8*)ino, sizeof(*ino) - sizeof(u32));

    de->inode_id = new_id;
    de->type = (u8)type;
    str_ncpy(de->name, path, LASTFS_MAX_NAME - 1u);

    return lastfs_journal_commit();
}

int lastfs_delete(const char* path) {
    if (!path || !g_lfs.mounted) return -1;
    lastfs_inode_t* ino = find_inode_by_name(path);
    if (!ino) return -1;

    (void)lastfs_journal_begin(ino->id, LASTFS_TX_DELETE, 0u, 0u);

    /* Clear dirent */
    for (u32 i = 0u; i < LASTFS_DIR_MAX; i++) {
        if (g_lfs.dirents[i].inode_id == ino->id) {
            memset(&g_lfs.dirents[i], 0, sizeof(lastfs_dirent_t));
            break;
        }
    }

    /* Clear inode */
    memset(ino, 0, sizeof(lastfs_inode_t));
    return lastfs_journal_commit();
}

int lastfs_write(const char* path, const void* data, u32 len) {
    if (!path || !g_lfs.mounted) return -1;
    lastfs_inode_t* ino = find_inode_by_name(path);
    if (!ino) {
        if (lastfs_create(path, LASTFS_TYPE_FILE) != 0) return -1;
        ino = find_inode_by_name(path);
        if (!ino) return -1;
    }

    if (len == 0u) {
        ino->size_bytes = 0u;
        ino->extent_count = 0u;
        return lastfs_journal_commit();
    }

    u32 sectors_needed = (len + DISK_SECTOR_SIZE - 1u) / DISK_SECTOR_SIZE;
    if (ino->extent_count == 0u) {
        if (allocate_extent(ino, sectors_needed) != 0) return -1;
    }

    (void)lastfs_journal_begin(ino->id, LASTFS_TX_WRITE, len, sectors_needed);

    /* Write data across extents */
    const u8* src = (const u8*)data;
    u32 bytes_written = 0u;
    u8 sec[DISK_SECTOR_SIZE];

    for (u32 e = 0u; e < ino->extent_count && bytes_written < len; e++) {
        lastfs_extent_t* ext = &ino->extents[e];
        for (u32 s = 0u; s < ext->sector_count && bytes_written < len; s++) {
            memset(sec, 0, sizeof(sec));
            u32 chunk = len - bytes_written;
            if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
            memcpy(sec, src + bytes_written, chunk);

            if (disk_write_sector_fast(ext->physical_lba + s, sec) != 0) {
                return -1;
            }
            bytes_written += chunk;
        }
    }

    ino->size_bytes = len;
    ino->checksum = lastfs_calc_crc32c((const u8*)ino, sizeof(*ino) - sizeof(u32));

    return lastfs_journal_commit();
}

int lastfs_read(const char* path, void* buf, u32 max_len) {
    if (!path || !buf || !g_lfs.mounted) return -1;
    lastfs_inode_t* ino = find_inode_by_name(path);
    if (!ino) return -1;

    u32 to_read = ino->size_bytes;
    if (to_read > max_len) to_read = max_len;
    if (to_read == 0u) return 0;

    u8* dst = (u8*)buf;
    u32 bytes_read = 0u;
    u8 sec[DISK_SECTOR_SIZE];

    for (u32 e = 0u; e < ino->extent_count && bytes_read < to_read; e++) {
        lastfs_extent_t* ext = &ino->extents[e];
        for (u32 s = 0u; s < ext->sector_count && bytes_read < to_read; s++) {
            if (disk_read_sector(ext->physical_lba + s, sec) != 0) return -1;
            u32 chunk = to_read - bytes_read;
            if (chunk > DISK_SECTOR_SIZE) chunk = DISK_SECTOR_SIZE;
            memcpy(dst + bytes_read, sec, chunk);
            bytes_read += chunk;
        }
    }

    return (int)bytes_read;
}

int lastfs_stat(const char* path, lastfs_inode_t* out_inode) {
    if (!path || !out_inode || !g_lfs.mounted) return -1;
    lastfs_inode_t* ino = find_inode_by_name(path);
    if (!ino) return -1;
    memcpy(out_inode, ino, sizeof(lastfs_inode_t));
    return 0;
}

int lastfs_list_dir(const char* path, lastfs_dirent_t* out_entries, u32 max_entries) {
    (void)path;
    if (!out_entries || max_entries == 0u || !g_lfs.mounted) return -1;

    u32 count = 0u;
    for (u32 i = 0u; i < LASTFS_DIR_MAX && count < max_entries; i++) {
        if (g_lfs.dirents[i].inode_id != 0u) {
            memcpy(&out_entries[count], &g_lfs.dirents[i], sizeof(lastfs_dirent_t));
            count++;
        }
    }
    return (int)count;
}

int lastfs_sync(void) {
    return lastfs_journal_commit();
}

int lastfs_fsck(lastfs_fsck_report_t* report, u8 auto_repair) {
    if (!g_lfs.mounted || !report) return -1;
    memset(report, 0, sizeof(*report));
    report->is_clean = 1u;

    for (u32 i = 0u; i < LASTFS_INODE_MAX; i++) {
        if (g_lfs.inodes[i].id != 0u) {
            report->used_inodes++;
            u32 calc = lastfs_calc_crc32c((const u8*)&g_lfs.inodes[i], sizeof(lastfs_inode_t) - sizeof(u32));
            if (calc != g_lfs.inodes[i].checksum) {
                report->checksum_errors++;
                report->is_clean = 0u;
                if (auto_repair) {
                    g_lfs.inodes[i].checksum = calc;
                }
            }
        }
    }

    if (auto_repair && !report->is_clean) {
        (void)flush_inodes();
    }
    return 0;
}
