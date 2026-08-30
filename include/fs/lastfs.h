#ifndef LASTFS_H
#define LASTFS_H

#include "types.h"

#define LASTFS_MAGIC            0x5346544Cu  /* "LTFS" in Little-Endian */
#define LASTFS_V2_MAGIC         0x3253544Cu  /* "LTS2" (LastFS 2.0) */
#define LASTFS_VERSION_1        1u
#define LASTFS_VERSION_2        2u

#define LASTFS_MAX_NAME         32u
#define LASTFS_INODE_MAX        128u
#define LASTFS_DIR_MAX          128u
#define LASTFS_MAX_EXTENTS      8u
#define LASTFS_JOURNAL_ENTRIES  64u

/* File Types */
#define LASTFS_TYPE_FILE        1u
#define LASTFS_TYPE_DIR         2u
#define LASTFS_TYPE_SYMLINK     3u

/* Journal Transaction Types */
typedef enum {
    LASTFS_TX_NONE = 0,
    LASTFS_TX_CREATE,
    LASTFS_TX_WRITE,
    LASTFS_TX_DELETE,
    LASTFS_TX_TRUNCATE,
    LASTFS_TX_EXTENT_ALLOC,
    LASTFS_TX_COMMIT
} lastfs_tx_type_t;

/* Extent Descriptor for Contiguous Block Allocation */
typedef struct {
    u32 logical_block;   /* Logical sector offset within the file */
    u32 physical_lba;    /* Starting physical LBA on disk */
    u32 sector_count;    /* Number of contiguous sectors */
    u32 checksum;        /* CRC32C/FNV1a checksum of the extent metadata */
} __attribute__((packed)) lastfs_extent_t;

/* LastFS 2.0 Inode */
typedef struct {
    u32 id;
    u32 type;            /* 1=file, 2=dir, 3=symlink */
    u32 size_bytes;
    u32 flags;           /* Bit 0: Extents enabled, Bit 1: Compressed, Bit 2: Encrypted */
    u32 extent_count;
    lastfs_extent_t extents[LASTFS_MAX_EXTENTS];
    char name[LASTFS_MAX_NAME];
    u32 created_tick;
    u32 modified_tick;
    u32 checksum;
} __attribute__((packed)) lastfs_inode_t;

/* LastFS 2.0 Directory Entry */
typedef struct {
    u32 inode_id;
    u8 type;
    char name[LASTFS_MAX_NAME];
} __attribute__((packed)) lastfs_dirent_t;

/* LastFS 2.0 Journal Entry Header */
typedef struct {
    u32 tx_id;
    u32 seq;
    u16 tx_type;
    u16 flags;
    u32 target_inode;
    u32 arg0;
    u32 arg1;
    u32 checksum;
} __attribute__((packed)) lastfs_journal_entry_t;

/* LastFS 2.0 Superblock */
typedef struct {
    u32 magic;               /* LASTFS_V2_MAGIC */
    u32 version;             /* 2 */
    u32 total_lba;           /* Total partition sectors */
    u32 block_size;          /* Logical block size (usually 512 or 4096) */
    u32 inode_lba;           /* Starting LBA of inode table */
    u32 inode_count;         /* Total inodes supported */
    u32 dir_lba;             /* Starting LBA of root directory table */
    u32 dir_count;           /* Total directory entries */
    u32 journal_lba;         /* Starting LBA of WAL journal ring */
    u32 journal_sectors;     /* Size of journal ring in sectors */
    u32 data_lba;            /* Starting LBA of data region */
    u32 free_lba;            /* Next unallocated data LBA */
    u32 dirty_flag;          /* 0 = Clean unmount, 1 = Needs recovery replay */
    u32 checksum;            /* Superblock integrity checksum */
} __attribute__((packed)) lastfs_super_t;

/* Legacy Seed Plan for installation */
typedef struct {
    u32 start_lba;
    u32 total_lba;
    u32 boot_src_lba;
    u32 boot_src_count;
    u32 kernel_src_lba;
    u32 kernel_src_count;
} lastfs_seed_plan_t;

/* FSCK Report */
typedef struct {
    u32 total_inodes;
    u32 used_inodes;
    u32 orphaned_extents;
    u32 journal_replayed_txs;
    u32 checksum_errors;
    u8 is_clean;
} lastfs_fsck_report_t;

/* ── LastFS 2.0 Core Public API ── */
int  lastfs_format(const lastfs_seed_plan_t* plan);
int  lastfs_format_v2(u32 start_lba, u32 total_lba);
int  lastfs_mount(u32 start_lba, u32 total_lba);
void lastfs_unmount(void);
int  lastfs_is_mounted(void);

/* File Operations */
int  lastfs_create(const char* path, u32 type);
int  lastfs_delete(const char* path);
int  lastfs_read(const char* path, void* buf, u32 max_len);
int  lastfs_write(const char* path, const void* data, u32 len);
int  lastfs_stat(const char* path, lastfs_inode_t* out_inode);
int  lastfs_list_dir(const char* path, lastfs_dirent_t* out_entries, u32 max_entries);
int  lastfs_sync(void);

/* Journal & Crash Consistency */
int  lastfs_journal_begin(u32 inode_id, lastfs_tx_type_t tx_type, u32 arg0, u32 arg1);
int  lastfs_journal_commit(void);
int  lastfs_journal_replay(void);

/* Verification & Integrity */
int  lastfs_fsck(lastfs_fsck_report_t* report, u8 auto_repair);
u32  lastfs_calc_crc32c(const u8* data, u32 len);

#endif /* LASTFS_H */
