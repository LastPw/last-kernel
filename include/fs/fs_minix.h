#ifndef FS_MINIX_H
#define FS_MINIX_H

#include "types.h"
#include "mount.h"

/* On-disk super block */
struct minix_super_block {
    u16 s_ninodes;
    u16 s_nzones;
    u16 s_imap_blocks;
    u16 s_zmap_blocks;
    u16 s_firstdatazone;
    u16 s_log_zone_size;
    u32 s_max_size;
    u16 s_magic;
    u16 s_state;
    u32 s_zones;
};

struct minix3_super_block {
    u32 s_ninodes;
    u16 s_pad0;
    u16 s_imap_blocks;
    u16 s_zmap_blocks;
    u16 s_firstdatazone;
    u16 s_log_zone_size;
    u32 s_pad1;
    u32 s_max_size;
    u32 s_zones;
    u16 s_magic;
    u16 s_pad2;
    u16 s_blocksize;
    u8 s_disk_version;
};

/* On-disk inode (V1 = 32 bytes) */
struct minix_inode {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_time;
    u8  i_gid;
    u8  i_nlinks;
    u16 i_zone[9];
};

/* On-disk inode (V2 = 64 bytes) */
struct minix2_inode {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_mtime;
    u32 i_atime;
    u32 i_ctime;
    u16 i_gid;
    u16 i_nlinks;
    u32 i_zone[10];
};

/* On-disk directory entry (V1, 14-char names) */
struct minix_dir_entry {
    u16 inode;
    char name[14];
};

/* On-disk directory entry (V1, 30-char names) */
struct minix_dir_entry2 {
    u16 inode;
    char name[30];
};

/* On-disk directory entry (V3, 60-char names) */
struct minix3_dir_entry {
    u32 inode;
    char name[60];
};

/* Magic numbers */
#define MINIX_SUPER_MAGIC    0x137F
#define MINIX_SUPER_MAGIC2   0x138F
#define MINIX2_SUPER_MAGIC   0x2468
#define MINIX2_SUPER_MAGIC2  0x2478
#define MINIX3_SUPER_MAGIC   0x4D5A

#define MINIX_V1 0x0001
#define MINIX_V2 0x0002
#define MINIX_V3 0x0003

#define MINIX_ROOT_INO 1
#define MINIX_LINK_MAX 250
#define MINIX2_LINK_MAX 65535

#define MINIX_INODES_PER_BLOCK_V1 ((u32)(1024 / sizeof(struct minix_inode)))
#define MINIX_INODES_PER_BLOCK_V2 ((u32)(1024 / sizeof(struct minix2_inode)))

/* In-memory MINIX filesystem data */
typedef struct {
    u16 s_ninodes;
    u16 s_nzones;
    u16 s_imap_blocks;
    u16 s_zmap_blocks;
    u16 s_firstdatazone;
    u16 s_log_zone_size;
    u16 s_version;
    u16 s_dirsize;
    u16 s_namelen;
    u16 s_mount_state;
    u32 dev_id;
    u32 start_lba;
    u32 block_size;
    /* Bitmap buffer pointers (we cache them) */
    u8* imap;     /* inode bitmap data */
    u8* zmap;     /* zone bitmap data */
    u32 imap_bytes;
    u32 zmap_bytes;
} minix_fs_t;

/* MINIX driver functions */
int minix_mount(u32 dev_id, u32 start_lba, void** fs_data);
int minix_umount(void* fs_data);
int minix_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out);
int minix_read(void* fs_data, const char* path, char* out, u32 out_len);
int minix_write(void* fs_data, const char* path, const char* data, u32 len);
int minix_create(void* fs_data, const char* path, u8 is_dir);
int minix_delete(void* fs_data, const char* path);
int minix_info(void* fs_data, const char* path, vfs_node_info_t* out);
int minix_format(u32 dev_id, u32 start_lba, u32 total_lba);

#endif
