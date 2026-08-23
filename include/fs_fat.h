#ifndef FS_FAT_H
#define FS_FAT_H

#include "types.h"
#include "mount.h"

/* FAT constants from Linux msdos_fs.h */
#define ATTR_RO      0x01
#define ATTR_HIDDEN  0x02
#define ATTR_SYS     0x04
#define ATTR_VOLUME  0x08
#define ATTR_DIR     0x10
#define ATTR_ARCH    0x20
#define ATTR_UNUSED  0x3F

#define FAT_START_ENT   2
#define FAT12_MASK      0x0FFF
#define FAT16_MASK      0xFFFF
#define FAT32_MASK      0x0FFFFFFF

#define FAT12_EOC       0x0FF8
#define FAT16_EOC       0xFFF8
#define FAT32_EOC       0x0FFFFFF8

#define FAT12_BAD       0x0FF7
#define FAT16_BAD       0xFFF7
#define FAT32_BAD       0x0FFFFFF7

#define MAX_FAT12       0xFF4
#define MAX_FAT16       0xFFF4
#define MAX_FAT32       0x0FFFFFF4

#define MSDOS_NAME      11
#define MSDOS_DOT       ".          "
#define MSDOS_DOTDOT    "..         "

#define FAT_BS_BOOTSIG  0x29
#define FAT_BOOTCODE_LEN  0x1E0

/* FAT12/16 BPB */
struct fat_bpb {
    u8   jmp_boot[3];
    char oem_name[8];
    u16  bytes_per_sec;
    u8   sec_per_clus;
    u16  reserved_sec;
    u8   fats;
    u16  root_entries;
    u16  total_sec_16;
    u8   media;
    u16  fat_sec_16;
    u16  sec_per_trk;
    u16  num_heads;
    u32  hidden_sec;
    u32  total_sec_32;
} __attribute__((packed));

/* FAT32 extension */
struct fat32_ext {
    u32  fat_sec_32;
    u16  ext_flags;
    u16  fs_version;
    u32  root_cluster;
    u16  fs_info;
    u16  bk_boot_sec;
    u8   reserved[12];
    u8   drive_num;
    u8   reserved1;
    u8   boot_sig;
    u32  vol_id;
    char vol_label[11];
    char fs_type[8];
} __attribute__((packed));

/* Combined FAT BPB */
struct fat_super_block {
    struct fat_bpb bpb;
    union {
        struct fat32_ext fat32;
        u8 boot_code[FAT_BOOTCODE_LEN];
    } ext;
    u16 signature; /* 0xAA55 */
} __attribute__((packed));

/* On-disk directory entry */
struct msdos_dir_entry {
    u8   name[MSDOS_NAME];
    u8   attr;
    u8   nt_res;
    u8   crt_time_tenth;
    u16  crt_time;
    u16  crt_date;
    u16  lst_acc_date;
    u16  start_hi;
    u16  time;
    u16  date;
    u16  start;
    u32  size;
} __attribute__((packed));

/* VFAT long filename entry */
struct vfat_slot_entry {
    u8   id;
    u8   name0_4[10];
    u8   attr;
    u8   reserved;
    u8   alias_checksum;
    u8   name5_10[12];
    u16  start;
    u8   name11_12[4];
} __attribute__((packed));

/* In-memory FAT fs data */
typedef struct {
    u32  dev_id;
    u32  start_lba;
    u32  bytes_per_sec;
    u8   sec_per_clus;
    u32  cluster_size;
    u8   fats;
    u32  fat_start;
    u32  fat_length;
    u32  dir_start;
    u32  dir_entries;
    u32  data_start;
    u32  root_cluster;
    u32  max_cluster;
    u32  fat_bits;
} fat_fs_t;

/* FAT driver functions */
int fat_mount(u32 dev_id, u32 start_lba, void** fs_data);
int fat_umount(void* fs_data);
int fat_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out);
int fat_read(void* fs_data, const char* path, char* out, u32 out_len);
int fat_write(void* fs_data, const char* path, const char* data, u32 len);
int fat_create(void* fs_data, const char* path, u8 is_dir);
int fat_delete(void* fs_data, const char* path);
int fat_info(void* fs_data, const char* path, vfs_node_info_t* out);
int fat_format(u32 dev_id, u32 start_lba, u32 total_lba);

#endif
