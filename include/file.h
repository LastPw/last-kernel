#ifndef FILE_H
#define FILE_H

#include "types.h"
#include "vfs.h"

struct file_operations;
typedef struct file_operations file_operations_t;

#define FILE_MAX_OPEN 4096u

#define KFILE_READ      0x0001u
#define KFILE_WRITE     0x0002u
#define KFILE_APPEND    0x0004u
#define KFILE_CREATE    0x0008u
#define KFILE_TRUNC     0x0010u
#define KFILE_DIRECTORY 0x0020u

#define KSEEK_SET 0u
#define KSEEK_CUR 1u
#define KSEEK_END 2u

typedef struct {
    u32 fd;
    u32 flags;
    u32 offset;
    u32 refcount;
    u8 used;
    u8 type;
    char path[VFS_PATH_MAX];
} kfile_info_t;

void kfile_init(void);
int kfile_open(const char* path, u32 flags, u32* out_fd);
int kfile_open_for_task(const char* path, u32 flags, u32* out_fd, u32 owner_task);
int kfile_close(u32 fd);
int kfile_close_for_task(u32 fd, u32 owner_task);
int kfile_dup(u32 fd, u32* out_fd);
int kfile_dup_for_task(u32 fd, u32* out_fd, u32 owner_task);
void kfile_release_task(u32 owner_task);
int kfile_read(u32 fd, void* out, u32 len);
int kfile_read_for_task(u32 fd, void* out, u32 len, u32 owner_task);
int kfile_write(u32 fd, const void* data, u32 len);
int kfile_write_for_task(u32 fd, const void* data, u32 len, u32 owner_task);
int kfile_seek(u32 fd, i32 offset, u32 whence, u32* out_pos);
int kfile_seek_for_task(u32 fd, i32 offset, u32 whence, u32* out_pos, u32 owner_task);
int kfile_stat(u32 fd, vfs_node_info_t* out);
int kfile_stat_for_task(u32 fd, vfs_node_info_t* out, u32 owner_task);
int kfile_info(u32 fd, kfile_info_t* out);
int kfile_sync(u32 fd);
int kfile_sync_for_task(u32 fd, u32 owner_task);
int kfile_ioctl(u32 fd, u32 cmd, u64 arg);
int kfile_ioctl_for_task(u32 fd, u32 cmd, u64 arg, u32 owner_task);
int kfile_create_custom_fd(const char* name, u32 flags, u32 owner_task, const file_operations_t* fops, void* priv, u32* out_fd);

#endif
