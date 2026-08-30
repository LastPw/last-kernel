/* VFS-lite: the contract every filesystem driver implements.
   Deliberately small: probe/mount/lookup/read. No write paths, no
   dentry trees, no locks — boot loaders don't need them. */
#ifndef PULSE_VFS_H
#define PULSE_VFS_H

#include "pulsebase.h"

/* ---- block device shim (BIOS int13h / UEFI BlockIo / host file) ---- */
typedef struct {
    VOID   *ctx;                    /* backend-specific */
    UINT64  sector_size;
    UINT64  sectors;                /* volume extent, for bounds checks */
    /* read one LBA into buf (buf is at least sector_size bytes) */
    INT32 (*read_sector)(VOID *ctx, UINT64 lba, VOID *buf);
} blkdev_t;

/* ---- node: enough to read a file; driver keeps details in priv ---- */
#define VFS_PRIV_MAX 64

typedef struct {
    UINT64 size;
    UINT32 flags;                   /* VFS_F_* */
    UINT8  priv[VFS_PRIV_MAX];      /* inode/extents/cluster chain state */
} vfs_node_t;

#define VFS_F_FILE 0x1
#define VFS_F_DIR  0x2
#define VFS_F_RO   0x4

/* ---- error codes shared by all drivers ---- */
enum {
    FS_OK = 0,
    FS_E_PROBE,          /* signature not ours */
    FS_E_UNSUPPORTED,    /* recognized but uses a feature we refuse */
    FS_E_CORRUPT,
    FS_E_IO,
    FS_E_NOTFOUND,
    FS_E_RANGE,
    FS_E_NOMEM,
};

typedef struct {
    const char *name;               /* "fat32", "ext4", ... */

    /* Signature check against the device's first sectors. Must not
       allocate. Returns FS_OK or FS_E_PROBE. */
    INT32 (*probe)(const blkdev_t *dev);

    /* Mount into a caller-provided arena (cap 64 KiB). Stores whatever
       it needs there; no other allocations allowed. */
    INT32 (*mount)(const blkdev_t *dev, VOID *arena, UINTN arena_cap);

    /* Absolute path lookup, '/'-separated, e.g. "/boot/vmlinuz".
       Path length cap: 256. */
    INT32 (*lookup)(const char *path, vfs_node_t *out);

    /* Read up to len bytes at off; *got receives actual count. */
    INT32 (*read)(const vfs_node_t *node, UINT64 off,
                  VOID *buf, UINTN len, UINTN *got);
} fs_driver_t;

/* Registry order = probe priority; first accepting driver mounts. */
extern const fs_driver_t *const fs_drivers[];
extern const UINTN fs_driver_count;

#endif
