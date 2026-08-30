#ifndef FS_NTFS_H
#define FS_NTFS_H

#include "types.h"
#include "mount.h"

/* ==================== NTFS Magic Constants ==================== */
#define NTFS_OEM_ID               "NTFS    "  /* OEM ID in boot sector (8 bytes) */
#define NTFS_FILE_RECORD_MAGIC    0x454C4946u  /* "FILE" */
#define NTFS_INDEX_RECORD_MAGIC   0x4C494E49u  /* "INDX" */

/* MFT reference to the root directory */
#define NTFS_FILE_ROOT            5u

/* ==================== MFT Attribute Types ==================== */
#define NTFS_AT_STANDARD_INFO     0x10u
#define NTFS_AT_ATTRIBUTE_LIST    0x20u
#define NTFS_AT_FILE_NAME         0x30u
#define NTFS_AT_SECURITY_DESC     0x50u
#define NTFS_AT_VOLUME_NAME       0x60u
#define NTFS_AT_VOLUME_INFO       0x70u
#define NTFS_AT_DATA              0x80u
#define NTFS_AT_INDEX_ROOT        0x90u
#define NTFS_AT_INDEX_ALLOCATION  0xA0u
#define NTFS_AT_BITMAP            0xB0u
#define NTFS_AT_REPARSE_POINT     0xC0u

/* File record flags */
#define NTFS_FR_FLAG_IN_USE       0x01u
#define NTFS_FR_FLAG_DIRECTORY    0x02u

/* Index entry flags */
#define NTFS_IDX_FLAG_LAST        0x01u  /* Last entry in this block */
#define NTFS_IDX_FLAG_SUBDIR      0x02u  /* Entry is a subdirectory */

/* File name types */
#define NTFS_FN_POSIX             0u
#define NTFS_FN_WIN32             1u
#define NTFS_FN_DOS               2u
#define NTFS_FN_WIN32_AND_DOS     3u

/* Standard info flags for directories */
#define NTFS_STD_FLAG_DIRECTORY   0x10000000u

/* File permission modes */
#define NTFS_DIR_MODE             0755
#define NTFS_FILE_MODE            0644

/* Max number of data runs we can parse */
#define NTFS_MAX_RUNS             512u

/* ==================== On-Disk Structures ==================== */

/* NTFS Boot Sector (first 512 bytes of partition) */
typedef struct {
    u8  jmp_boot[3];        /* Jump instruction */
    u8  oem_id[8];          /* "NTFS    " */
    u16 bytes_per_sector;   /* Usually 512 */
    u8  sectors_per_cluster;/* 1, 2, 4, 8 (typical) */
    u16 reserved_sectors;   /* 0 for NTFS */
    u8  pad1[3];
    u16 unused1;
    u8  media_descriptor;
    u16 pad2;
    u16 sectors_per_track;
    u16 heads_per_cylinder;
    u32 hidden_sectors;
    u32 pad3;
    u32 pad4;
    u64 total_sectors;      /* Total number of sectors */
    u64 mft_lcn;            /* Cluster of $MFT */
    u64 mft_mirr_lcn;       /* Cluster of $MFTMirr */
    u8  clusters_per_mft_record;  /* Signed: neg = log2, pos = count */
    u8  pad5[3];
    u8  clusters_per_index_buffer;/* Signed: neg = log2, pos = count */
    u8  pad6[3];
    u64 volume_serial;
    u32 checksum;
    u8  boot_code[426];
    u16 boot_signature;     /* 0xAA55 */
} __attribute__((packed)) ntfs_boot_sector_t;

/* MFT File Record Header */
typedef struct {
    u32  magic;             /* "FILE" */
    u16  usa_offset;        /* Offset to Update Sequence Array */
    u16  usa_count;         /* Size of Update Sequence Array */
    u64  log_sequence_no;
    u16  sequence_no;
    u16  hard_link_count;
    u16  attr_offset;       /* Offset to first attribute */
    u16  flags;             /* In use, directory */
    u32  used_size;         /* Bytes used in this record */
    u32  alloc_size;        /* Bytes allocated for this record */
    u64  base_file_record;
    u16  next_attr_id;
    u16  pad;
    u32  record_no;         /* MFT index of this record */
} __attribute__((packed)) ntfs_file_record_t;

/* Attribute header (common part) */
typedef struct {
    u32  type;              /* Attribute type */
    u32  length;            /* Total attribute length (including header) */
    u8   non_resident;      /* 0 = resident, 1 = non-resident */
    u8   name_len;          /* Length of attribute name (0 if none) */
    u16  name_offset;       /* Offset to attribute name */
    u16  flags;             /* Attribute flags */
    u16  attr_id;           /* Attribute ID */
    /* Resident attributes follow here offset depends on non_resident */
} __attribute__((packed)) ntfs_attr_header_t;

/* Resident attribute */
typedef struct {
    u32  content_size;      /* Size of attribute content */
    u16  content_offset;    /* Offset to content from attr start */
    u8   indexed_flag;      /* Is attribute indexed? */
    u8   pad;
} __attribute__((packed)) ntfs_resident_attr_t;

/* Non-resident attribute */
typedef struct {
    u64  start_vcn;         /* Starting VCN */
    u64  last_vcn;          /* Last VCN */
    u16  run_offset;        /* Offset to data runs */
    u8   compression_unit;  /* Compression unit size */
    u8   pad[5];
    u64  alloc_size;        /* Allocated size */
    u64  real_size;         /* Actual data size */
    u64  init_size;         /* Initialized size */
} __attribute__((packed)) ntfs_nonresident_attr_t;

/* FILE_NAME attribute */
typedef struct {
    u64  parent_mft_ref;    /* MFT reference of parent */
    u64  creation_time;
    u64  last_mod_time;
    u64  last_change_time;
    u64  last_access_time;
    u64  alloc_size;        /* Allocated size */
    u64  real_size;         /* Actual size */
    u32  flags;             /* File flags */
    u32  ea_reparse;        /* EA and reparse info */
    u8   name_length;       /* Filename length in characters */
    u8   name_type;         /* Filename type (POSIX, WIN32, DOS) */
    u16  name[0];           /* Filename in UTF-16LE */
} __attribute__((packed)) ntfs_filename_attr_t;

/* Index entry header */
typedef struct {
    u64  mft_ref;           /* MFT reference of the file */
    u16  length;            /* Total entry length */
    u16  stream_length;     /* Length of stream (key data) */
    u8   flags;             /* Flags (last, subdir) */
    u8   pad[3];
    /* Key data (FILE_NAME attribute) follows */
} __attribute__((packed)) ntfs_index_entry_t;

/* Index root header (inside INDEX_ROOT attribute) */
typedef struct {
    u32  type;              /* Type of indexed attribute */
    u32  collation_rule;    /* Collation rule */
    u32  index_block_size;  /* Size of each index block */
    u8   clusters_per_block;/* Clusters per index block */
    u8   pad[3];
    /* Index header follows */
} __attribute__((packed)) ntfs_index_root_header_t;

/* Index header (start of the index entries) */
typedef struct {
    u32  first_entry_offset;/* Offset to first index entry from here */
    u32  total_size;        /* Total size of index entries + header */
    u32  alloc_size;        /* Allocated size */
    u32  flags;             /* 0x01 = large directory (has index alloc) */
    u8   entries[0];        /* Start of index entries */
} __attribute__((packed)) ntfs_index_header_t;

/* Index block header (for INDEX_ALLOCATION blocks) */
typedef struct {
    u32  magic;             /* "INDX" */
    u16  usa_offset;
    u16  usa_count;
    u64  log_sequence_no;
    u64  vcn;               /* VCN of this index block */
    /* Index header follows at offset 0x18 */
} __attribute__((packed)) ntfs_index_block_header_t;

/* Data run (variable-length encoding) */
typedef struct {
    u64  vcn;               /* Starting VCN (used during parsing) */
    u64  lcn;               /* Starting LCN (0 = sparse) */
    u64  length;            /* Number of clusters */
} ntfs_run_t;

/* ==================== In-Memory Data ==================== */

/* NTFS filesystem context */
typedef struct {
    u32  dev_id;
    u32  start_lba;
    u32  bytes_per_sector;
    u8   sectors_per_cluster;
    u32  bytes_per_cluster;
    u32  clusters_per_mft_record;      /* Positive value */
    u32  bytes_per_mft_record;
    u32  clusters_per_index_record;    /* Positive value */
    u32  bytes_per_index_record;
    u64  mft_lcn;            /* Cluster of $MFT */
    u64  total_sectors;
    u32  mft_record_size;    /* Actual MFT record size in bytes */
    u32  index_record_size;  /* Actual index record size in bytes */
} ntfs_fs_t;

/* ==================== Driver Functions ==================== */

int ntfs_mount(u32 dev_id, u32 start_lba, void** fs_data);
int ntfs_umount(void* fs_data);
int ntfs_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out);
int ntfs_read(void* fs_data, const char* path, char* out, u32 out_len);
int ntfs_write(void* fs_data, const char* path, const char* data, u32 len);
int ntfs_create(void* fs_data, const char* path, u8 is_dir);
int ntfs_delete(void* fs_data, const char* path);
int ntfs_info(void* fs_data, const char* path, vfs_node_info_t* out);
int ntfs_format(u32 dev_id, u32 start_lba, u32 total_lba);

#endif /* FS_NTFS_H */
