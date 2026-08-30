#ifndef FS_EXFAT_H
#define FS_EXFAT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EXFAT_SECTOR_SIZE        512u
#define EXFAT_ENTRY_SIZE         32u
#define EXFAT_MAX_NAME_LEN       255u

#define EXFAT_ENTRY_IN_USE       0x80u
#define EXFAT_ENTRY_TYPE_BITMAP  0x81u
#define EXFAT_ENTRY_TYPE_UPCASE  0x82u
#define EXFAT_ENTRY_TYPE_LABEL   0x83u
#define EXFAT_ENTRY_TYPE_FILE    0x85u
#define EXFAT_ENTRY_TYPE_STREAM  0xC0u
#define EXFAT_ENTRY_TYPE_NAME    0xC1u

#define EXFAT_FLAG_ALLOC_POSSIBLE 0x01u
#define EXFAT_FLAG_NO_FAT_CHAIN   0x02u

#define EXFAT_ATTR_RO            0x0001u
#define EXFAT_ATTR_HIDDEN        0x0002u
#define EXFAT_ATTR_SYSTEM        0x0004u
#define EXFAT_ATTR_VOLUME        0x0008u
#define EXFAT_ATTR_DIR           0x0010u
#define EXFAT_ATTR_ARCHIVE       0x0020u

#define EXFAT_CLUSTER_FREE       0x00000000u
#define EXFAT_CLUSTER_BAD        0xFFFFFFF7u
#define EXFAT_CLUSTER_EOC        0xFFFFFFFFu
#define EXFAT_CLUSTER_FIRST      2u

/* exFAT Volume Boot Record (VBR) - 512 Bytes */
typedef struct {
    u8  jump_boot[3];
    char fs_name[8];              /* "EXFAT   " */
    u8  must_be_zero[53];
    u64 partition_offset;         /* LBA offset of partition */
    u64 volume_length;            /* Total sectors in volume */
    u32 fat_offset;               /* Sector offset of FAT */
    u32 fat_length;               /* Sector length of FAT */
    u32 cluster_heap_offset;      /* Sector offset of cluster heap */
    u32 cluster_count;            /* Total clusters in heap */
    u32 root_dir_cluster;         /* Starting cluster of root directory */
    u32 volume_serial_number;
    u16 fs_revision;
    u16 volume_flags;
    u8  bytes_per_sector_shift;   /* Usually 9 (512B) */
    u8  sectors_per_cluster_shift;/* Usually 3 (8 sectors = 4KB cluster) */
    u8  number_of_fats;           /* Usually 1 */
    u8  drive_select;
    u8  percent_in_use;
    u8  reserved[7];
    u8  boot_code[390];
    u16 boot_signature;           /* 0xAA55 */
} __attribute__((packed)) exfat_vbr_t;

/* 32-Byte Allocation Bitmap Directory Entry (0x81) */
typedef struct {
    u8  entry_type;               /* 0x81 */
    u8  bitmap_flags;             /* 0 = First cluster allocation bitmap */
    u8  reserved[18];
    u32 first_cluster;            /* Starting cluster of bitmap */
    u64 data_length;              /* Length of bitmap in bytes */
} __attribute__((packed)) exfat_bitmap_entry_t;

/* 32-Byte Up-case Table Directory Entry (0x82) */
typedef struct {
    u8  entry_type;               /* 0x82 */
    u8  reserved1[3];
    u32 table_checksum;
    u8  reserved2[12];
    u32 first_cluster;            /* Starting cluster of up-case table */
    u64 data_length;              /* Length of table in bytes */
} __attribute__((packed)) exfat_upcase_entry_t;

/* 32-Byte Volume Label Directory Entry (0x83) */
typedef struct {
    u8  entry_type;               /* 0x83 */
    u8  character_count;          /* Length of volume label in characters */
    u16 volume_label[11];         /* Volume label in UTF-16LE */
    u8  reserved[8];
} __attribute__((packed)) exfat_label_entry_t;

/* 32-Byte Primary File Directory Entry (0x85) */
typedef struct {
    u8  entry_type;               /* 0x85 */
    u8  secondary_count;          /* Number of following secondary entries */
    u16 set_checksum;
    u16 file_attributes;
    u16 reserved1;
    u32 create_timestamp;
    u32 last_modified_timestamp;
    u32 last_access_timestamp;
    u8  create_10ms;
    u8  last_modified_10ms;
    u8  create_tz_offset;
    u8  last_modified_tz_offset;
    u8  last_access_tz_offset;
    u8  reserved2[7];
} __attribute__((packed)) exfat_file_entry_t;

/* 32-Byte Stream Extension Entry (0xC0) */
typedef struct {
    u8  entry_type;               /* 0xC0 */
    u8  general_flags;            /* Bit 1: NoFatChain (contiguous) */
    u8  reserved1;
    u8  name_length;              /* Number of characters in filename */
    u16 name_hash;
    u16 reserved2;
    u64 valid_data_length;
    u32 reserved3;
    u32 first_cluster;            /* Starting data cluster */
    u64 data_length;              /* 64-bit actual file size */
} __attribute__((packed)) exfat_stream_entry_t;

/* 32-Byte File Name Entry (0xC1) */
typedef struct {
    u8  entry_type;               /* 0xC1 */
    u8  general_flags;
    u16 file_name[15];            /* 15 Unicode UTF-16LE characters */
} __attribute__((packed)) exfat_name_entry_t;

/* In-Memory File / Inode Representation */
typedef struct {
    u32 parent_dir_cluster;
    u32 dir_entry_offset;         /* Byte offset within parent directory */
    u16 attributes;
    u32 first_cluster;
    u64 size;
    u64 valid_size;
    u8  no_fat_chain;
    char name[EXFAT_MAX_NAME_LEN + 1];
} exfat_inode_t;

/* In-Memory exFAT State */
typedef struct {
    u8   is_mounted;
    u32  start_lba;
    u32  bytes_per_sector;
    u32  sectors_per_cluster;
    u32  bytes_per_cluster;
    u32  fat_offset_lba;
    u32  fat_length_sectors;
    u32  cluster_heap_lba;
    u32  total_clusters;
    u32  root_dir_cluster;
    u32  bitmap_first_cluster;
    u64  bitmap_size_bytes;
    u32  upcase_first_cluster;
    u64  upcase_size_bytes;
    char volume_label[33];
    u8*  bitmap_cache;            /* In-memory cached allocation bitmap */
} exfat_fs_t;

/* Checksum and Hash Utilities */
u16  exfat_calc_checksum(const u8* entry_set, u32 entry_count);
u16  exfat_calc_name_hash(const char* ascii_name);

/* Filesystem Lifecycle & Format */
int  exfat_mount(u32 start_lba, exfat_fs_t* out_fs);
int  exfat_unmount(exfat_fs_t* fs);
int  exfat_format(u32 start_lba, u32 total_sectors, const char* label, exfat_fs_t* out_fs);
int  exfat_sync(exfat_fs_t* fs);

/* Cluster Allocator (Allocation Bitmap & FAT) */
u32  exfat_alloc_cluster(exfat_fs_t* fs, u32 prev_cluster, u8 is_contiguous);
int  exfat_free_cluster_chain(exfat_fs_t* fs, u32 first_cluster, u64 size, u8 no_fat_chain);

/* Path & Inode Operations */
int  exfat_lookup(exfat_fs_t* fs, const char* path, exfat_inode_t* out_inode);
int  exfat_create(exfat_fs_t* fs, const char* path, u16 attributes, exfat_inode_t* out_inode);
int  exfat_mkdir(exfat_fs_t* fs, const char* path, exfat_inode_t* out_inode);
int  exfat_unlink(exfat_fs_t* fs, const char* path);

/* File I/O Engine */
int  exfat_read(exfat_fs_t* fs, const exfat_inode_t* inode, u64 offset, u8* buffer, u64 size, u64* out_read);
int  exfat_write(exfat_fs_t* fs, exfat_inode_t* inode, u64 offset, const u8* buffer, u64 size, u64* out_written);
int  exfat_truncate(exfat_fs_t* fs, exfat_inode_t* inode, u64 new_size);

/* Global Telemetry */
void exfat_get_global_status(exfat_fs_t* out_fs);

#ifdef __cplusplus
}
#endif

#endif /* FS_EXFAT_H */
