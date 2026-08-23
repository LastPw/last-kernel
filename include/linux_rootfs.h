#ifndef LINUX_ROOTFS_H
#define LINUX_ROOTFS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROOTFS_MAX_ENTRIES 64
#define ROOTFS_MAX_PATH 128

typedef enum {
    ROOTFS_FILE_STATIC = 0,
    ROOTFS_FILE_DYNAMIC_LIBC,
    ROOTFS_FILE_DYNAMIC_LIBM,
    ROOTFS_FILE_DYNAMIC_PTHREAD,
    ROOTFS_FILE_DYNAMIC_WAYLAND,
    ROOTFS_FILE_ETC_CONFIG
} rootfs_type_t;

typedef struct {
    u8 active;
    char virtual_path[ROOTFS_MAX_PATH];
    const u8* content_ptr;
    u32 content_size;
    rootfs_type_t type;
    u8 is_directory;
} rootfs_entry_t;

/* ── Linux RootFS & Shared Library Subsystem APIs ── */
int linux_rootfs_init(void);
int linux_rootfs_register_file(const char* vpath, const u8* data, u32 size, rootfs_type_t type);
const rootfs_entry_t* linux_rootfs_lookup(const char* vpath);
int linux_rootfs_read(const char* vpath, u8* out_buf, u32 max_len, u32 offset);

#ifdef __cplusplus
}
#endif

#endif /* LINUX_ROOTFS_H */
