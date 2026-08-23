#include "file.h"
#include "mount.h"
#include "devfs.h"
#include "string.h"

#define FILE_DATA_LIMIT VFS_FILE_MAX

typedef struct {
    u8  used;
    u8  type;
    u8  is_device;
    u8  reserved;
    u32 flags;
    u32 offset;
    u32 refcount;
    u32 owner_task;
    char path[VFS_PATH_MAX];
    const file_operations_t* fops;
    void* dev_priv;
} kfile_t;

static kfile_t g_files[FILE_MAX_OPEN];

static int valid_fd(u32 fd) {
    return fd < FILE_MAX_OPEN && g_files[fd].used && g_files[fd].refcount != 0u;
}

static int fd_allowed(u32 fd, u32 owner_task) {
    if (!valid_fd(fd)) return 0;
    return owner_task == 0u || g_files[fd].owner_task == owner_task;
}

static int alloc_fd(void) {
    for (u32 i = 0u; i < FILE_MAX_OPEN; i++) {
        if (!g_files[i].used) return (int)i;
    }
    return -1;
}

static int can_read(const kfile_t* f) {
    return f && (f->flags & KFILE_READ) != 0u;
}

static int can_write(const kfile_t* f) {
    return f && (f->flags & KFILE_WRITE) != 0u;
}

/* Keep the descriptor layer usable for both the built-in LastFS and mounted drivers. */
static int file_info_path(const char* path, vfs_node_info_t* out) {
    if (fs_info(path, out) == 0) return 0;
    return vfs_info(path, out);
}

static int file_create_path(const char* path) {
    if (fs_create(path, 0u) == 0) return 0;
    return vfs_create_file(path);
}

static int file_read_path(const char* path, char* out, u32 len) {
    int n = fs_read(path, out, len);
    return n >= 0 ? n : vfs_read_file(path, out, len);
}

static int file_write_path(const char* path, const char* data, u32 len) {
    int n = fs_write(path, data, len);
    return n >= 0 ? n : vfs_write_file(path, data, len);
}

void kfile_init(void) {
    memset(g_files, 0, sizeof(g_files));
}

int kfile_open(const char* path, u32 flags, u32* out_fd) {
    return kfile_open_for_task(path, flags, out_fd, 0u);
}

int kfile_open_for_task(const char* path, u32 flags, u32* out_fd, u32 owner_task) {
    if (!path || !path[0] || !out_fd) return -1;
    if ((flags & (KFILE_READ | KFILE_WRITE)) == 0u) return -1;

    /* Check if this is a /dev device path */
    if (devfs_is_dev_path(path)) {
        const file_operations_t* fops = 0;
        void* priv = 0;
        if (devfs_open(path, flags, &priv, &fops) != 0 || !fops) {
            return -1;
        }

        int fd = alloc_fd();
        if (fd < 0) return -1;
        kfile_t* f = &g_files[(u32)fd];
        memset(f, 0, sizeof(*f));
        f->used = 1u;
        f->type = VFS_NODE_FILE;
        f->is_device = 1u;
        f->flags = flags;
        f->offset = 0u;
        f->refcount = 1u;
        f->owner_task = owner_task;
        f->fops = fops;
        f->dev_priv = priv;
        str_lcpy(f->path, path, VFS_PATH_MAX);
        *out_fd = (u32)fd;
        return 0;
    }

    vfs_node_info_t info;
    int exists = file_info_path(path, &info) == 0;
    if (!exists && (flags & KFILE_CREATE) == 0u) return -1;
    if (!exists) {
        if (file_create_path(path) != 0 || file_info_path(path, &info) != 0) return -1;
    }
    if (info.type == VFS_NODE_DIR && (flags & KFILE_DIRECTORY) == 0u) return -1;
    if (info.type != VFS_NODE_DIR && (flags & KFILE_DIRECTORY) != 0u) return -1;

    if ((flags & KFILE_READ) && vfs_access(path, VFS_ACC_READ) != 0) return -1;
    if ((flags & KFILE_WRITE) && vfs_access(path, VFS_ACC_WRITE) != 0) return -1;
    if (info.type == VFS_NODE_DIR && vfs_access(path, VFS_ACC_EXEC) != 0) return -1;

    int fd = alloc_fd();
    if (fd < 0) return -1;
    kfile_t* f = &g_files[(u32)fd];
    memset(f, 0, sizeof(*f));
    f->used = 1u;
    f->type = (u8)info.type;
    f->is_device = 0u;
    f->flags = flags;
    f->offset = (flags & KFILE_APPEND) ? info.size : 0u;
    f->refcount = 1u;
    f->owner_task = owner_task;
    str_lcpy(f->path, path, VFS_PATH_MAX);

    if ((flags & KFILE_TRUNC) && can_write(f) && info.type == VFS_NODE_FILE) {
        if (file_write_path(f->path, "", 0u) < 0) {
            memset(f, 0, sizeof(*f));
            return -1;
        }
        f->offset = 0u;
    }
    *out_fd = (u32)fd;
    return 0;
}

int kfile_close(u32 fd) {
    return kfile_close_for_task(fd, 0u);
}

int kfile_close_for_task(u32 fd, u32 owner_task) {
    if (!fd_allowed(fd, owner_task)) return -1;
    if (g_files[fd].refcount > 1u) {
        g_files[fd].refcount--;
        return 0;
    }
    if (g_files[fd].is_device && g_files[fd].fops && g_files[fd].fops->close) {
        g_files[fd].fops->close(g_files[fd].dev_priv);
    }
    memset(&g_files[fd], 0, sizeof(g_files[fd]));
    return 0;
}

int kfile_dup(u32 fd, u32* out_fd) {
    return kfile_dup_for_task(fd, out_fd, 0u);
}

int kfile_dup_for_task(u32 fd, u32* out_fd, u32 owner_task) {
    if (!fd_allowed(fd, owner_task) || !out_fd) return -1;
    int new_fd = alloc_fd();
    if (new_fd < 0) return -1;
    g_files[(u32)new_fd] = g_files[fd];
    g_files[(u32)new_fd].refcount = 1u;
    g_files[(u32)new_fd].owner_task = owner_task;
    *out_fd = (u32)new_fd;
    return 0;
}

void kfile_release_task(u32 owner_task) {
    if (owner_task == 0u) return;
    for (u32 i = 0u; i < FILE_MAX_OPEN; i++) {
        if (g_files[i].used && g_files[i].owner_task == owner_task) {
            if (g_files[i].is_device && g_files[i].fops && g_files[i].fops->close) {
                g_files[i].fops->close(g_files[i].dev_priv);
            }
            memset(&g_files[i], 0, sizeof(g_files[i]));
        }
    }
}

int kfile_read(u32 fd, void* out, u32 len) {
    return kfile_read_for_task(fd, out, len, 0u);
}

int kfile_read_for_task(u32 fd, void* out, u32 len, u32 owner_task) {
    if (!fd_allowed(fd, owner_task) || !out || !can_read(&g_files[fd]) || g_files[fd].type == VFS_NODE_DIR) return -1;
    if (len == 0u) return 0;

    /* Route device read */
    if (g_files[fd].is_device) {
        if (g_files[fd].fops && g_files[fd].fops->read) {
            i32 n = g_files[fd].fops->read(g_files[fd].dev_priv, (char*)out, len, (u64)g_files[fd].offset);
            if (n > 0) g_files[fd].offset += (u32)n;
            return (int)n;
        }
        return -1;
    }

    char data[FILE_DATA_LIMIT];
    vfs_node_info_t info;
    if (file_info_path(g_files[fd].path, &info) != 0) return -1;
    if (g_files[fd].offset >= info.size) return 0;

    if (fs_info(g_files[fd].path, &info) != 0) {
        u32 available = info.size - g_files[fd].offset;
        u32 count = len < available ? len : available;
        int n = vfs_read_file_at(g_files[fd].path, g_files[fd].offset, (char*)out, count);
        if (n < 0) return -1;
        g_files[fd].offset += (u32)n;
        return n;
    }

    int n = file_read_path(g_files[fd].path, data, sizeof(data));
    if (n < 0 || g_files[fd].offset >= (u32)n) return 0;
    u32 available = (u32)n - g_files[fd].offset;
    u32 count = len < available ? len : available;
    memcpy(out, data + g_files[fd].offset, count);
    g_files[fd].offset += count;
    return (int)count;
}

int kfile_write(u32 fd, const void* data, u32 len) {
    return kfile_write_for_task(fd, data, len, 0u);
}

int kfile_write_for_task(u32 fd, const void* data, u32 len, u32 owner_task) {
    if (!fd_allowed(fd, owner_task) || !data || !can_write(&g_files[fd]) || g_files[fd].type == VFS_NODE_DIR) return -1;
    if (len == 0u) return 0;

    /* Route device write */
    if (g_files[fd].is_device) {
        if (g_files[fd].fops && g_files[fd].fops->write) {
            i32 n = g_files[fd].fops->write(g_files[fd].dev_priv, (const char*)data, len, (u64)g_files[fd].offset);
            if (n > 0) g_files[fd].offset += (u32)n;
            return (int)n;
        }
        return -1;
    }

    vfs_node_info_t info;
    char merged[FILE_DATA_LIMIT];
    if (file_info_path(g_files[fd].path, &info) != 0) return -1;
    if (len > FILE_DATA_LIMIT) len = FILE_DATA_LIMIT;
    if ((g_files[fd].flags & KFILE_APPEND) || g_files[fd].offset != 0u) {
        int old = file_read_path(g_files[fd].path, merged, sizeof(merged));
        if (old < 0) return -1;
        u32 start = (g_files[fd].flags & KFILE_APPEND) ? (u32)old : g_files[fd].offset;
        if (start > FILE_DATA_LIMIT) return -1;
        if (start + len > FILE_DATA_LIMIT) len = FILE_DATA_LIMIT - start;
        memcpy(merged + start, data, len);
        u32 written = len;
        u32 final_size = start + written;
        if (final_size < (u32)old) {
            final_size = (u32)old;
        } else {
            final_size = start + written;
        }
        if (file_write_path(g_files[fd].path, merged, final_size) < 0) return -1;
        g_files[fd].offset = start + written;
        return (int)written;
    }
    if (file_write_path(g_files[fd].path, (const char*)data, len) < 0) return -1;
    g_files[fd].offset = len;
    return (int)len;
}

int kfile_seek(u32 fd, i32 offset, u32 whence, u32* out_pos) {
    return kfile_seek_for_task(fd, offset, whence, out_pos, 0u);
}

int kfile_seek_for_task(u32 fd, i32 offset, u32 whence, u32* out_pos, u32 owner_task) {
    if (!fd_allowed(fd, owner_task) || g_files[fd].type == VFS_NODE_DIR) return -1;
    if (g_files[fd].is_device) {
        /* Device seek */
        if (whence == KSEEK_SET) {
            if (offset < 0) return -1;
            g_files[fd].offset = (u32)offset;
        } else if (whence == KSEEK_CUR) {
            i32 next = (i32)g_files[fd].offset + offset;
            if (next < 0) return -1;
            g_files[fd].offset = (u32)next;
        }
        if (out_pos) *out_pos = g_files[fd].offset;
        return 0;
    }

    vfs_node_info_t info;
    if (file_info_path(g_files[fd].path, &info) != 0) return -1;
    i32 base = 0;
    if (whence == KSEEK_CUR) base = (i32)g_files[fd].offset;
    else if (whence == KSEEK_END) base = (i32)info.size;
    else if (whence != KSEEK_SET) return -1;
    if (offset < 0 && base < -offset) return -1;
    i32 next = base + offset;
    if (next < 0) return -1;
    g_files[fd].offset = (u32)next;
    if (out_pos) *out_pos = g_files[fd].offset;
    return 0;
}

int kfile_stat(u32 fd, vfs_node_info_t* out) {
    return kfile_stat_for_task(fd, out, 0u);
}

int kfile_stat_for_task(u32 fd, vfs_node_info_t* out, u32 owner_task) {
    if (!fd_allowed(fd, owner_task) || !out) return -1;
    if (g_files[fd].is_device) {
        memset(out, 0, sizeof(*out));
        out->used = 1;
        out->type = VFS_NODE_FILE;
        out->mode = 0666;
        str_lcpy(out->name, g_files[fd].path, VFS_MAX_NAME);
        return 0;
    }
    return file_info_path(g_files[fd].path, out);
}

int kfile_info(u32 fd, kfile_info_t* out) {
    if (!valid_fd(fd) || !out) return -1;
    memset(out, 0, sizeof(*out));
    out->fd = fd;
    out->flags = g_files[fd].flags;
    out->offset = g_files[fd].offset;
    out->refcount = g_files[fd].refcount;
    out->used = g_files[fd].used;
    out->type = g_files[fd].type;
    str_lcpy(out->path, g_files[fd].path, VFS_PATH_MAX);
    return 0;
}

int kfile_sync(u32 fd) {
    return kfile_sync_for_task(fd, 0u);
}

int kfile_sync_for_task(u32 fd, u32 owner_task) {
    if (!fd_allowed(fd, owner_task)) return -1;
    if (g_files[fd].is_device) return 0;
    return vfs_flush_disk();
}

int kfile_ioctl(u32 fd, u32 cmd, u64 arg) {
    return kfile_ioctl_for_task(fd, cmd, arg, 0u);
}

int kfile_ioctl_for_task(u32 fd, u32 cmd, u64 arg, u32 owner_task) {
    if (!fd_allowed(fd, owner_task)) return -1;
    kfile_t* f = &g_files[fd];
    if (f->is_device && f->fops && f->fops->ioctl) {
        return f->fops->ioctl(f->dev_priv, cmd, arg);
    }
    return -1;
}

int kfile_create_custom_fd(const char* name, u32 flags, u32 owner_task, const file_operations_t* fops, void* priv, u32* out_fd) {
    if (!out_fd || !fops) return -1;
    int fd = alloc_fd();
    if (fd < 0) return -1;
    kfile_t* f = &g_files[(u32)fd];
    memset(f, 0, sizeof(*f));
    f->used = 1u;
    f->type = VFS_NODE_FILE;
    f->is_device = 1u;
    f->flags = flags;
    f->offset = 0u;
    f->refcount = 1u;
    f->owner_task = owner_task;
    f->fops = fops;
    f->dev_priv = priv;
    if (name) {
        str_lcpy(f->path, name, VFS_PATH_MAX);
    } else {
        str_lcpy(f->path, "custom_fd", VFS_PATH_MAX);
    }
    *out_fd = (u32)fd;
    return 0;
}
