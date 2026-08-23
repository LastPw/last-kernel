#ifndef MOUNT_H
#define MOUNT_H

#include "types.h"
#include "vfs.h"

#define FS_MAX_NAME 8
#define FS_MAX_MOUNTS 8
#define FS_MAX_DRIVERS 8
#define FS_MAX_PATH 96

typedef enum {
    FS_TYPE_NONE = 0,
    FS_TYPE_MINIX = 1,
    FS_TYPE_EXT2 = 2,
    FS_TYPE_FAT = 3,
    FS_TYPE_NTFS = 4,
    FS_TYPE_EXT4 = 5,
    FS_TYPE_EXT3 = 6
} fs_type_t;

/* Filesystem driver interface - implemented by each filesystem type */
typedef struct {
    char name[FS_MAX_NAME];
    fs_type_t type;
    int (*mount)(u32 dev_id, u32 start_lba, void** fs_data);
    int (*umount)(void* fs_data);
    int (*list)(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out);
    int (*read)(void* fs_data, const char* path, char* out, u32 out_len);
    int (*write)(void* fs_data, const char* path, const char* data, u32 len);
    int (*create)(void* fs_data, const char* path, u8 is_dir);
    int (*delete)(void* fs_data, const char* path);
    int (*info)(void* fs_data, const char* path, vfs_node_info_t* out);
    int (*format)(u32 dev_id, u32 start_lba, u32 total_lba);
} fs_driver_t;

/* Mount entry */
typedef struct {
    u8 used;
    u8 dev_id;
    u32 start_lba;
    char mount_point[FS_MAX_PATH];
    fs_type_t type;
    void* fs_data;
    const fs_driver_t* driver;
} mount_entry_t;

/* Mount API */
int mount_init(void);
int fs_auto_mount(void);
int fs_register_driver(const fs_driver_t* driver);
int fs_mount(const char* mount_point, const char* fstype, u32 dev_id, u32 start_lba);
int fs_umount(const char* mount_point);
int fs_format(const char* fstype, u32 dev_id, u32 start_lba, u32 total_lba);
int fs_resolve(const char* path, mount_entry_t** mnt, char* rel_path, u32 rel_sz);
int fs_list(const char* path, vfs_node_info_t* out, u32 max_out);
int fs_read(const char* path, char* out, u32 out_len);
int fs_write(const char* path, const char* data, u32 len);
int fs_create(const char* path, u8 is_dir);
int fs_delete(const char* path);
int fs_info(const char* path, vfs_node_info_t* out);
int fs_mount_status(mount_entry_t* out, u32 max_out);
u32 fs_mount_count(void);

#endif
