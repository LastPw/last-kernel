#include "mount.h"
#include "fs_iso9660.h"
#include "fs_minix.h"
#include "fs_ext2.h"
#include "fs_fat.h"
#include "fs_ntfs.h"
#include "string.h"
#include "disk.h"
#include "serial.h"

static mount_entry_t g_mounts[FS_MAX_MOUNTS];
static const fs_driver_t* g_drivers[FS_MAX_DRIVERS];
static u32 g_driver_count = 0;
static u32 g_mount_count = 0;

static void path_normalize(const char* path, char* out, u32 out_sz) {
    u32 i = 0, j = 0;
    if (!path || !out || out_sz == 0) return;
    out[j++] = '/';
    if (path[0] == '/') i = 1;
    while (path[i] && j + 1 < out_sz) {
        if (path[i] == '/') {
            while (path[i] == '/') i++;
            if (j > 1) { out[j++] = '/'; }
        } else {
            out[j++] = path[i++];
        }
    }
    if (j == 0) out[j++] = '/';
    out[j] = '\0';
}

static void str_to_lower(char* s) {
    if (!s) return;
    for (u32 i = 0; s[i]; i++) {
        if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] - 'A' + 'a');
    }
}

static const fs_driver_t* find_driver(const char* fstype) {
    char lower[FS_MAX_NAME];
    str_ncpy(lower, fstype, FS_MAX_NAME - 1);
    lower[FS_MAX_NAME - 1] = '\0';
    str_to_lower(lower);
    for (u32 i = 0; i < g_driver_count; i++) {
        char dl[FS_MAX_NAME];
        str_ncpy(dl, g_drivers[i]->name, FS_MAX_NAME - 1);
        dl[FS_MAX_NAME - 1] = '\0';
        str_to_lower(dl);
        if (str_cmp(lower, dl) == 0) return g_drivers[i];
    }
    return 0;
}

int mount_init(void) {
    for (u32 i = 0; i < FS_MAX_MOUNTS; i++) {
        g_mounts[i].used = 0;
    }
    g_driver_count = 0;
    g_mount_count = 0;

    /* Register built-in filesystem drivers */
    {
        static fs_driver_t minix_drv;
        str_cpy(minix_drv.name, "minix");
        minix_drv.type = FS_TYPE_MINIX;
        minix_drv.mount = minix_mount;
        minix_drv.umount = minix_umount;
        minix_drv.list = minix_list;
        minix_drv.read = minix_read;
        minix_drv.write = minix_write;
        minix_drv.create = minix_create;
        minix_drv.delete = minix_delete;
        minix_drv.info = minix_info;
        minix_drv.format = minix_format;
        fs_register_driver(&minix_drv);
    }
    {
        static fs_driver_t ext2_drv;
        str_cpy(ext2_drv.name, "ext2");
        ext2_drv.type = FS_TYPE_EXT2;
        ext2_drv.mount = ext2_mount;
        ext2_drv.umount = ext2_umount;
        ext2_drv.list = ext2_list;
        ext2_drv.read = ext2_read;
        ext2_drv.write = ext2_write;
        ext2_drv.create = ext2_create;
        ext2_drv.delete = ext2_delete;
        ext2_drv.info = ext2_info;
        ext2_drv.format = ext2_format;
        fs_register_driver(&ext2_drv);
    }
    {
        static fs_driver_t ext4_drv;
        str_cpy(ext4_drv.name, "ext4");
        ext4_drv.type = FS_TYPE_EXT4;
        ext4_drv.mount = ext2_mount;
        ext4_drv.umount = ext2_umount;
        ext4_drv.list = ext2_list;
        ext4_drv.read = ext2_read;
        ext4_drv.write = ext2_write;
        ext4_drv.create = ext2_create;
        ext4_drv.delete = ext2_delete;
        ext4_drv.info = ext2_info;
        ext4_drv.format = ext2_format;
        fs_register_driver(&ext4_drv);
    }
    {
        static fs_driver_t ext3_drv;
        str_cpy(ext3_drv.name, "ext3");
        ext3_drv.type = FS_TYPE_EXT3;
        ext3_drv.mount = ext2_mount;
        ext3_drv.umount = ext2_umount;
        ext3_drv.list = ext2_list;
        ext3_drv.read = ext2_read;
        ext3_drv.write = ext2_write;
        ext3_drv.create = ext2_create;
        ext3_drv.delete = ext2_delete;
        ext3_drv.info = ext2_info;
        ext3_drv.format = ext2_format;
        fs_register_driver(&ext3_drv);
    }
    {
        static fs_driver_t fat_drv;
        str_cpy(fat_drv.name, "vfat");
        fat_drv.type = FS_TYPE_FAT;
        fat_drv.mount = fat_mount;
        fat_drv.umount = fat_umount;
        fat_drv.list = fat_list;
        fat_drv.read = fat_read;
        fat_drv.write = fat_write;
        fat_drv.create = fat_create;
        fat_drv.delete = fat_delete;
        fat_drv.info = fat_info;
        fat_drv.format = fat_format;
        fs_register_driver(&fat_drv);
    }
    {
        static fs_driver_t ntfs_drv;
        str_cpy(ntfs_drv.name, "ntfs");
        ntfs_drv.type = FS_TYPE_NTFS;
        ntfs_drv.mount = ntfs_mount;
        ntfs_drv.umount = ntfs_umount;
        ntfs_drv.list = ntfs_list;
        ntfs_drv.read = ntfs_read;
        ntfs_drv.write = ntfs_write;
        ntfs_drv.create = ntfs_create;
        ntfs_drv.delete = ntfs_delete;
        ntfs_drv.info = ntfs_info;
        ntfs_drv.format = ntfs_format;
        fs_register_driver(&ntfs_drv);
    }
    {
        static fs_driver_t iso_drv;
        str_cpy(iso_drv.name, "iso9660");
        iso_drv.type = FS_TYPE_ISO9660;
        iso_drv.mount = iso9660_mount;
        iso_drv.umount = iso9660_umount;
        iso_drv.list = iso9660_list;
        iso_drv.read = iso9660_read;
        iso_drv.write = iso9660_write;
        iso_drv.create = iso9660_create;
        iso_drv.delete = iso9660_delete;
        iso_drv.info = iso9660_info;
        iso_drv.format = iso9660_format;
        fs_register_driver(&iso_drv);
    }
    return 0;
}

int fs_auto_mount(void) {
    u32 count = disk_partition_count();
    u32 mounted = 0u;
    for (u32 i = 0u; i < count && i < DISK_MAX_PARTITIONS; i++) {
        disk_partition_info_t part;
        char mount_point[FS_MAX_PATH];
        const char* type = 0;
        if (disk_partition_get(i, &part) != 0 || !part.valid) continue;
        if (part.fs == DISK_FS_EXT4) type = "ext4";
        if (!type) continue;
        mount_point[0] = '/'; mount_point[1] = 'm'; mount_point[2] = 'n'; mount_point[3] = 't';
        mount_point[4] = '/'; mount_point[5] = 'p';
        mount_point[6] = (char)('0' + (i / 10u));
        mount_point[7] = (char)('0' + (i % 10u));
        mount_point[8] = '\0';
        if (fs_mount(mount_point, type, 0u, part.start_lba) == 0) mounted++;
    }
    return (int)mounted;
}

int fs_register_driver(const fs_driver_t* driver) {
    if (!driver || g_driver_count >= FS_MAX_DRIVERS) return -1;
    g_drivers[g_driver_count++] = driver;
    return 0;
}

int fs_mount(const char* mount_point, const char* fstype, u32 dev_id, u32 start_lba) {
    if (!mount_point || !fstype) return -1;

    char norm[FS_MAX_PATH];
    path_normalize(mount_point, norm, sizeof(norm));
    if (norm[0] != '/') return -1;

    /* Check if already mounted at this point */
    for (u32 i = 0; i < FS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && str_cmp(g_mounts[i].mount_point, norm) == 0) {
            return -1; /* Already mounted */
        }
    }

    const fs_driver_t* drv = find_driver(fstype);
    if (!drv) { serial_writeln("[LastOS] mount driver missing"); return -1; }

    /* Find empty slot */
    int slot = -1;
    for (u32 i = 0; i < FS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) { slot = (int)i; break; }
    }
    if (slot < 0) return -1;

    void* fs_data = 0;
    if (drv->mount(dev_id, start_lba, &fs_data) != 0) { serial_writeln("[LastOS] mount driver failed"); return -1; }
    if (!fs_data) return -1;

    g_mounts[slot].used = 1;
    g_mounts[slot].dev_id = dev_id;
    g_mounts[slot].start_lba = start_lba;
    str_ncpy(g_mounts[slot].mount_point, norm, FS_MAX_PATH - 1);
    g_mounts[slot].mount_point[FS_MAX_PATH - 1] = '\0';
    g_mounts[slot].type = drv->type;
    g_mounts[slot].fs_data = fs_data;
    g_mounts[slot].driver = drv;
    g_mount_count++;
    return 0;
}

int fs_umount(const char* mount_point) {
    if (!mount_point) return -1;
    char norm[FS_MAX_PATH];
    path_normalize(mount_point, norm, sizeof(norm));

    for (u32 i = 0; i < FS_MAX_MOUNTS; i++) {
        if (g_mounts[i].used && str_cmp(g_mounts[i].mount_point, norm) == 0) {
            if (g_mounts[i].driver && g_mounts[i].driver->umount) {
                (void)g_mounts[i].driver->umount(g_mounts[i].fs_data);
            }
            g_mounts[i].used = 0;
            g_mounts[i].fs_data = 0;
            g_mounts[i].driver = 0;
            g_mount_count--;
            return 0;
        }
    }
    return -1;
}

int fs_format(const char* fstype, u32 dev_id, u32 start_lba, u32 total_lba) {
    if (!fstype) return -1;
    const fs_driver_t* drv = find_driver(fstype);
    if (!drv || !drv->format) return -1;
    return drv->format(dev_id, start_lba, total_lba);
}

int fs_resolve(const char* path, mount_entry_t** mnt, char* rel_path, u32 rel_sz) {
    if (!path || !mnt || !rel_path) return -1;
    char norm[FS_MAX_PATH];
    path_normalize(path, norm, sizeof(norm));
    *mnt = 0;
    rel_path[0] = '\0';

    int best = -1;
    u32 best_len = 0;

    for (u32 i = 0; i < FS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) continue;
        u32 mlen = str_len(g_mounts[i].mount_point);
        if (str_ncmp(norm, g_mounts[i].mount_point, mlen) == 0 &&
            (norm[mlen] == '\0' || norm[mlen] == '/') && mlen > best_len) {
            best = (int)i;
            best_len = mlen;
        }
    }

    if (best < 0) return -1;

    *mnt = &g_mounts[best];
    /* Build relative path */
    const char* rest = norm + best_len;
    if (rest[0] == '\0') {
        str_ncpy(rel_path, "/", rel_sz);
    } else {
        str_ncpy(rel_path, rest, rel_sz);
    }
    rel_path[rel_sz - 1] = '\0';
    return 0;
}

int fs_list(const char* path, vfs_node_info_t* out, u32 max_out) {
    mount_entry_t* mnt;
    char rel[FS_MAX_PATH];
    if (fs_resolve(path, &mnt, rel, sizeof(rel)) != 0 || !mnt) return -1;
    if (!mnt->driver || !mnt->driver->list) return -1;
    return mnt->driver->list(mnt->fs_data, rel, out, max_out);
}

int fs_read(const char* path, char* out, u32 out_len) {
    mount_entry_t* mnt;
    char rel[FS_MAX_PATH];
    if (fs_resolve(path, &mnt, rel, sizeof(rel)) != 0 || !mnt) return -1;
    if (!mnt->driver || !mnt->driver->read) return -1;
    return mnt->driver->read(mnt->fs_data, rel, out, out_len);
}

int fs_write(const char* path, const char* data, u32 len) {
    mount_entry_t* mnt;
    char rel[FS_MAX_PATH];
    if (fs_resolve(path, &mnt, rel, sizeof(rel)) != 0 || !mnt) return -1;
    if (!mnt->driver || !mnt->driver->write) return -1;
    return mnt->driver->write(mnt->fs_data, rel, data, len);
}

int fs_create(const char* path, u8 is_dir) {
    mount_entry_t* mnt;
    char rel[FS_MAX_PATH];
    if (fs_resolve(path, &mnt, rel, sizeof(rel)) != 0 || !mnt) return -1;
    if (!mnt->driver || !mnt->driver->create) return -1;
    return mnt->driver->create(mnt->fs_data, rel, is_dir);
}

int fs_delete(const char* path) {
    mount_entry_t* mnt;
    char rel[FS_MAX_PATH];
    if (fs_resolve(path, &mnt, rel, sizeof(rel)) != 0 || !mnt) return -1;
    if (!mnt->driver || !mnt->driver->delete) return -1;
    return mnt->driver->delete(mnt->fs_data, rel);
}

int fs_info(const char* path, vfs_node_info_t* out) {
    mount_entry_t* mnt;
    char rel[FS_MAX_PATH];
    if (fs_resolve(path, &mnt, rel, sizeof(rel)) != 0 || !mnt) return -1;
    if (!mnt->driver || !mnt->driver->info) return -1;
    return mnt->driver->info(mnt->fs_data, rel, out);
}

int fs_mount_status(mount_entry_t* out, u32 max_out) {
    if (!out) return -1;
    u32 c = 0;
    for (u32 i = 0; i < FS_MAX_MOUNTS && c < max_out; i++) {
        if (!g_mounts[i].used) continue;
        out[c] = g_mounts[i];
        c++;
    }
    return (int)c;
}

u32 fs_mount_count(void) {
    return g_mount_count;
}
