#ifndef FS_ISO9660_H
#define FS_ISO9660_H

#include "types.h"
#include "mount.h"

/* ISO9660 read-only filesystem driver.
 * Supports: primary volume descriptors, directory-record walking,
 * multi-extent files, and Rock Ridge NM (long-name) entries.
 * Inherently read-only: write/create/delete/format report -EROFS-style -1. */

#define ISO_LOGICAL_BLOCK 2048u

int iso9660_mount(u32 dev_id, u32 start_lba, void** fs_data);
int iso9660_umount(void* fs_data);
int iso9660_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out);
int iso9660_read(void* fs_data, const char* path, char* out, u32 out_len);
int iso9660_write(void* fs_data, const char* path, const char* data, u32 len);
int iso9660_create(void* fs_data, const char* path, u8 is_dir);
int iso9660_delete(void* fs_data, const char* path);
int iso9660_info(void* fs_data, const char* path, vfs_node_info_t* out);
int iso9660_format(u32 dev_id, u32 start_lba, u32 total_lba);

#endif
