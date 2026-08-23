#ifndef FS_EXT2_H
#define FS_EXT2_H

#include "types.h"
#include "mount.h"

/* Constants from Linux kernel ext2_fs.h */
#define EXT2_SUPER_MAGIC      0xEF53
#define EXT2_ROOT_INO         2
#define EXT2_GOOD_OLD_REV     0
#define EXT2_DYNAMIC_REV      1
#define EXT2_GOOD_OLD_INODE_SIZE 128
#define EXT2_GOOD_OLD_FIRST_INO 11
#define EXT2_NDIR_BLOCKS      12
#define EXT2_IND_BLOCK        12
#define EXT2_DIND_BLOCK       13
#define EXT2_TIND_BLOCK       14
#define EXT2_N_BLOCKS         15
#define EXT2_NAME_LEN         255
#define EXT2_DIR_PAD          4
#define EXT2_DIR_ROUND        (EXT2_DIR_PAD - 1)
#define EXT2_DIR_REC_LEN(n)   ((((n) + 8 + EXT2_DIR_ROUND) & ~EXT2_DIR_ROUND))
#define EXT2_MIN_BLOCK_SIZE   1024
#define EXT2_MAX_BLOCK_SIZE   4096

/* File types for directory entries */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_SYMLINK  7

/* Inode modes */
#define EXT2_S_IFMT   0xF000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFLNK  0xA000

/* Inode flags (ext3/ext4) */
#define EXT4_EXTENTS_FL      0x00080000u  /* Inode uses extent tree */
#define EXT4_EA_INODE_FL     0x00200000u  /* Inode used for large EA */
#define EXT4_EXTENT_MAX_DEPTH 5u

/* Feature flags */
#define EXT4_FEATURE_INCOMPAT_EXTENTS   0x0040u  /* Extent mapping */
#define EXT4_FEATURE_INCOMPAT_64BIT     0x0080u
#define EXT4_FEATURE_INCOMPAT_FLEX_BG   0x0200u
#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001u
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE   0x0002u
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE    0x0008u
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM     0x0010u
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK    0x0020u
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE  0x0040u

/* Extent tree structures */
struct ext4_extent_header {
    u16  eh_magic;        /* 0xF30A */
    u16  eh_entries;      /* Number of entries in this node */
    u16  eh_max;          /* Maximum entries in this node */
    u16  eh_depth;        /* 0 = leaf, >0 = index node */
    u32  eh_generation;
} __attribute__((packed));

/* Extent tree leaf node entry */
struct ext4_extent {
    u32  ee_block;        /* First logical block covered */
    u16  ee_len;          /* Number of blocks covered (0-32768) */
    u16  ee_start_hi;     /* Upper 16 bits of physical block */
    u32  ee_start_lo;     /* Lower 32 bits of physical block */
} __attribute__((packed));

/* Extent tree index node entry (for non-leaf nodes) */
struct ext4_extent_idx {
    u32  ei_block;        /* First logical block covered */
    u32  ei_leaf_lo;      /* Lower 32 bits of child block */
    u16  ei_leaf_hi;      /* Upper 16 bits of child block */
    u16  ei_unused;
} __attribute__((packed));

/* Size of the i_block area reserved for extent tree root */
#define EXT4_EXTENT_ROOT_SIZE  (EXT2_N_BLOCKS * 4u)  /* 60 bytes */

/* ==================== JBD2 Journal Structures ==================== */

/* Journal magic number */
#define JBD2_MAGIC_NUMBER    0xC03B3998u

/* Journal block types */
#define JBD2_DESCRIPTOR_BLOCK  1u
#define JBD2_COMMIT_BLOCK      2u
#define JBD2_REVOKE_BLOCK      3u
#define JBD2_SUPERBLOCK_V2     4u

/* Journal tag flags */
#define JBD2_FLAG_ESCAPE       0x01u   /* Block data was escaped */
#define JBD2_FLAG_SAME_UUID    0x02u   /* Same UUID as journal superblock */
#define JBD2_FLAG_DELETED      0x04u   /* Block was deleted */
#define JBD2_FLAG_LAST_TAG     0x08u   /* Last tag in descriptor */

/* Journal superblock (block 0 of journal) */
struct jbd2_superblock_s {
    u32  s_header;          /* JBD2_MAGIC_NUMBER */
    u32  s_blocksize;       /* Journal block size */
    u32  s_maxlen;          /* Total blocks in journal */
    u32  s_first;           /* First block of the log */
    u32  s_sequence;        /* First sequence number to use */
    u32  s_start;           /* Start block of the log (0 = empty) */
    u32  s_errno;           /* Error number */
} __attribute__((packed));

/* Journal block header (at start of descriptor, commit, revoke blocks) */
struct jbd2_header_s {
    u32  h_magic;           /* JBD2_MAGIC_NUMBER */
    u32  h_blocktype;       /* DESCRIPTOR_BLOCK, COMMIT_BLOCK, REVOKE_BLOCK */
    u32  h_sequence;        /* Transaction sequence number */
} __attribute__((packed));

/* Journal descriptor block tag (each tag = one metadata block) */
struct jbd2_tag_s {
    u32  t_blocknr;         /* Block number in the filesystem */
    u32  t_flags;           /* JBD2_FLAG_* */
} __attribute__((packed));

/* Journal commit block tail */
struct jbd2_commit_tail_s {
    u32  t_magic;           /* JBD2_MAGIC_NUMBER */
    u32  t_sequence;        /* Sequence number */
    u32  t_checksum;        /* Checksum (zero if unused) */
} __attribute__((packed));

/* Max journal iteration to prevent infinite loops */
#define JBD2_MAX_TRANSACTIONS  1024u

/* On-disk superblock */
struct ext2_super_block {
    u32  s_inodes_count;
    u32  s_blocks_count;
    u32  s_r_blocks_count;
    u32  s_free_blocks_count;
    u32  s_free_inodes_count;
    u32  s_first_data_block;
    u32  s_log_block_size;
    u32  s_log_frag_size;
    u32  s_blocks_per_group;
    u32  s_frags_per_group;
    u32  s_inodes_per_group;
    u32  s_mtime;
    u32  s_wtime;
    u16  s_mnt_count;
    u16  s_max_mnt_count;
    u16  s_magic;
    u16  s_state;
    u16  s_errors;
    u16  s_minor_rev_level;
    u32  s_lastcheck;
    u32  s_checkinterval;
    u32  s_creator_os;
    u32  s_rev_level;
    u16  s_def_resuid;
    u16  s_def_resgid;
    u32  s_first_ino;
    u16  s_inode_size;
    u16  s_block_group_nr;
    u32  s_feature_compat;
    u32  s_feature_incompat;
    u32  s_feature_ro_compat;
    u8   s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    u32  s_algorithm_usage_bitmap;
    u8   s_prealloc_blocks;
    u8   s_prealloc_dir_blocks;
    u16  s_padding1;
    u8   s_journal_uuid[16];
    u32  s_journal_inum;
    u32  s_journal_dev;
    u32  s_last_orphan;
    u32  s_hash_seed[4];
    u8   s_def_hash_version;
    u8   s_reserved_char_pad;
    u16  s_reserved_word_pad;
    u32  s_default_mount_opts;
    u32  s_first_meta_bg;
    u32  s_reserved[190];
} __attribute__((packed));

/* On-disk block group descriptor */
struct ext2_group_desc {
    u32  bg_block_bitmap;
    u32  bg_inode_bitmap;
    u32  bg_inode_table;
    u16  bg_free_blocks_count;
    u16  bg_free_inodes_count;
    u16  bg_used_dirs_count;
    u16  bg_pad;
    u32  bg_reserved[3];
} __attribute__((packed));

/* On-disk inode (128 bytes for rev 0) */
struct ext2_inode {
    u16  i_mode;
    u16  i_uid;
    u32  i_size;
    u32  i_atime;
    u32  i_ctime;
    u32  i_mtime;
    u32  i_dtime;
    u16  i_gid;
    u16  i_links_count;
    u32  i_blocks;
    u32  i_flags;
    u32  i_osd1;
    u32  i_block[EXT2_N_BLOCKS];
    u32  i_generation;
    u32  i_file_acl;
    u32  i_dir_acl;
    u32  i_faddr;
    u8   i_osd2[12];
} __attribute__((packed));

/* On-disk directory entry (version 2 with file_type) */
struct ext2_dir_entry_2 {
    u32  inode;
    u16  rec_len;
    u8   name_len;
    u8   file_type;
    char name[0];
} __attribute__((packed));

/* In-memory ext2 data */
typedef struct {
    u32  blocks_count;
    u32  inodes_count;
    u32  blocks_per_group;
    u32  inodes_per_group;
    u32  inode_size;
    u32  block_size;
    u32  block_size_bits;
    u32  group_count;
    u32  first_data_block;
    u32  inodes_per_block;
    u32  itb_per_group;
    u32  desc_per_block;
    u32  gdb_count;
    u32  dev_id;
    u32  start_lba;
    u32  groups_mounted;
    /* Group descriptors cache */
    struct ext2_group_desc* group_desc;
    u32  gd_count;
    u8   use_extents;       /* ext4: use extent tree instead of block map */
    u8   has_flex_bg;       /* ext4: flex_bg feature */
} ext2_fs_t;

/* EXT2 driver functions */
int ext2_mount(u32 dev_id, u32 start_lba, void** fs_data);
int ext2_umount(void* fs_data);
int ext2_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out);
int ext2_read(void* fs_data, const char* path, char* out, u32 out_len);
int ext2_write(void* fs_data, const char* path, const char* data, u32 len);
int ext2_create(void* fs_data, const char* path, u8 is_dir);
int ext2_delete(void* fs_data, const char* path);
int ext2_info(void* fs_data, const char* path, vfs_node_info_t* out);
int ext2_format(u32 dev_id, u32 start_lba, u32 total_lba);

#endif
