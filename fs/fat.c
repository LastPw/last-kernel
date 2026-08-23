#include "fs_fat.h"
#include "advanced_allocator.h"
#include "disk.h"
#include "string.h"

/* Read sectors */
static int fat_read_sectors(fat_fs_t* fs, u32 lba, u8* buffer, u32 count) {
    u32 abs_lba = fs->start_lba + lba;
    for (u32 i = 0; i < count; i++) {
        if (disk_read_sector(abs_lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

/* Read cluster */
static int fat_read_cluster(fat_fs_t* fs, u32 cluster, u8* buffer) {
    u32 sector;
    if (fs->fat_bits == 32) {
        sector = fs->data_start + (cluster - FAT_START_ENT) * fs->sec_per_clus;
    } else {
        sector = fs->data_start + (cluster - FAT_START_ENT) * fs->sec_per_clus;
    }
    return fat_read_sectors(fs, sector, buffer, fs->sec_per_clus);
}

/* Read FAT entry */
static u32 fat_get_next(fat_fs_t* fs, u32 cluster) {
    u8 buf[DISK_SECTOR_SIZE];
    u32 fat_offset;
    u32 sector;
    if (fs->fat_bits == 12) {
        fat_offset = cluster + (cluster / 2);
        sector = fs->fat_start + (fat_offset / DISK_SECTOR_SIZE);
        if (fat_read_sectors(fs, sector, buf, 1) != 0) return 0x0FF7;
        if (cluster & 1) {
            return ((u32)buf[fat_offset % DISK_SECTOR_SIZE + 1] << 4) |
                   ((u32)buf[fat_offset % DISK_SECTOR_SIZE] >> 4);
        } else {
            return ((u32)buf[fat_offset % DISK_SECTOR_SIZE + 1] << 8) |
                   (u32)buf[fat_offset % DISK_SECTOR_SIZE];
        }
    } else if (fs->fat_bits == 16) {
        fat_offset = cluster * 2;
        sector = fs->fat_start + (fat_offset / DISK_SECTOR_SIZE);
        if (fat_read_sectors(fs, sector, buf, 1) != 0) return 0xFFF7;
        return (u32)buf[fat_offset % DISK_SECTOR_SIZE] |
               ((u32)buf[fat_offset % DISK_SECTOR_SIZE + 1] << 8);
    } else {
        fat_offset = cluster * 4;
        sector = fs->fat_start + (fat_offset / DISK_SECTOR_SIZE);
        if (fat_read_sectors(fs, sector, buf, 1) != 0) return 0x0FFFFFF7;
        return (u32)buf[fat_offset % DISK_SECTOR_SIZE] |
               ((u32)buf[fat_offset % DISK_SECTOR_SIZE + 1] << 8) |
               ((u32)buf[fat_offset % DISK_SECTOR_SIZE + 2] << 16) |
               ((u32)buf[fat_offset % DISK_SECTOR_SIZE + 3] << 24);
    }
}

static u32 fat_is_eoc(fat_fs_t* fs, u32 cluster) {
    if (fs->fat_bits == 12) return cluster >= 0x0FF8;
    if (fs->fat_bits == 16) return cluster >= 0xFFF8;
    return cluster >= 0x0FFFFFF8;
}

/* Convert FAT-style name to normal string (remove spaces, lowercase) */
static void fat_name_to_str(const u8* fat_name, u32 len, char* out, u32 out_sz) {
    u32 j = 0;
    for (u32 i = 0; i < len && i < MSDOS_NAME && j + 1 < out_sz; i++) {
        char c = (char)fat_name[i];
        if (c == ' ' || c == 0) continue;
        /* Check for VFAT long filename */
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = '\0';
}

/* Lookup file in a directory */
static u32 fat_dir_find_entry(fat_fs_t* fs, u32 dir_cluster, const char* name,
                               struct msdos_dir_entry* out_de) {
    u8 buf[8192]; /* Max cluster size */
    u32 cluster = dir_cluster;

    while (cluster != 0 && !fat_is_eoc(fs, cluster)) {
        if (fat_read_cluster(fs, cluster, buf) != 0) break;

        u32 entries_per_cluster = (fs->cluster_size) / sizeof(struct msdos_dir_entry);
        struct msdos_dir_entry* de = (struct msdos_dir_entry*)buf;

        for (u32 i = 0; i < entries_per_cluster; i++) {
            if (de[i].name[0] == 0) return 0; /* End of directory */
            if (de[i].name[0] == 0xE5) continue; /* Deleted entry */

            if (de[i].attr == ATTR_VOLUME) continue;
            if (de[i].attr & ATTR_VOLUME) {
                /* Could be VFAT long filename entry - skip for now */
                continue;
            }

            char entry_name[13];
            fat_name_to_str(de[i].name, MSDOS_NAME, entry_name, sizeof(entry_name));

            /* Check for matching name */
            if (str_cmp(entry_name, name) == 0) {
                if (out_de) *out_de = de[i];
                /* Return cluster number */
                u32 start = (u32)de[i].start | ((u32)de[i].start_hi << 16);
                return start;
            }
        }
        cluster = fat_get_next(fs, cluster);
    }
    return 0;
}

/* Resolve path to cluster (for directories) or 0 */
static u32 fat_path_to_cluster(fat_fs_t* fs, const char* path,
                                struct msdos_dir_entry* out_de) {
    if (!path || path[0] != '/') return 0;
    if (str_cmp(path, "/") == 0) return fs->root_cluster;

    char name[256];
    u32 cur_cluster = fs->root_cluster;
    u32 i = 1;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        u32 ni = 0;
        while (path[i] && path[i] != '/' && ni < 255) {
            name[ni++] = path[i++];
        }
        name[ni] = '\0';
        if (ni == 0) continue;

        struct msdos_dir_entry de;
        u32 next = fat_dir_find_entry(fs, cur_cluster, name, &de);
        if (next == 0) return 0;

        if (path[i] == '\0' || path[i] == '/') {
            /* Last component */
            if (out_de) *out_de = de;
            return next;
        }
        cur_cluster = next;
    }
    return cur_cluster;
}

/* Read file data given starting cluster */
static int fat_read_file(fat_fs_t* fs, u32 start_cluster, char* out, u32 out_len) {
    if (start_cluster == 0) return 0;

    u8 buf[8192];
    u32 cluster = start_cluster;
    u32 done = 0;

    while (!fat_is_eoc(fs, cluster)) {
        if (fat_read_cluster(fs, cluster, buf) != 0) break;

        u32 chunk = fs->cluster_size;
        if (done + chunk > out_len) chunk = out_len - done;
        memcpy(out + done, buf, chunk);
        done += chunk;
        if (done >= out_len) break;

        cluster = fat_get_next(fs, cluster);
        if (cluster == 0 || cluster == 1) break;
    }

    if (done < out_len) out[done] = '\0';
    return (int)done;
}

/* Fill VFS info from directory entry */
static void fat_de_to_info(fat_fs_t* fs, const struct msdos_dir_entry* de,
                            u32 cluster, vfs_node_info_t* out) {
    (void)fs;
    if (!out) return;
    memset(out, 0, sizeof(vfs_node_info_t));
    out->used = 1;
    out->id = cluster;
    out->size = de->size;
    out->type = (de->attr & ATTR_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->mode = (de->attr & ATTR_RO) ? 0555 : 0755;
}

/* List directory */
static int fat_dir_list(fat_fs_t* fs, u32 dir_cluster, vfs_node_info_t* out, u32 max_out) {
    u8 buf[8192];
    u32 cluster = dir_cluster;
    u32 count = 0;

    while (cluster != 0 && !fat_is_eoc(fs, cluster) && count < max_out) {
        if (fat_read_cluster(fs, cluster, buf) != 0) break;

        u32 entries_per_cluster = fs->cluster_size / sizeof(struct msdos_dir_entry);
        struct msdos_dir_entry* de = (struct msdos_dir_entry*)buf;

        for (u32 i = 0; i < entries_per_cluster && count < max_out; i++) {
            if (de[i].name[0] == 0) goto done;
            if (de[i].name[0] == 0xE5) continue;
            if (de[i].attr == ATTR_VOLUME) continue;
            if (de[i].attr & ATTR_VOLUME) continue; /* Skip VFAT */

            out[count].used = 1;
            u32 start = (u32)de[i].start | ((u32)de[i].start_hi << 16);
            out[count].id = start;
            out[count].size = de[i].size;
            out[count].type = (de[i].attr & ATTR_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
            out[count].mode = (de[i].attr & ATTR_RO) ? 0555 : 0755;

            fat_name_to_str(de[i].name, MSDOS_NAME, out[count].name, VFS_MAX_NAME);
            count++;
        }
        cluster = fat_get_next(fs, cluster);
    }
done:
    return (int)count;
}

/* Driver implementations */

int fat_mount(u32 dev_id, u32 start_lba, void** fs_data) {
    if (!fs_data) return -1;
    if (dev_id != 0) return -1;
    (void)disk_select(0);

    fat_fs_t* fs = (fat_fs_t*)adv_alloc(sizeof(fat_fs_t));
    if (!fs) return -1;
    memset(fs, 0, sizeof(fat_fs_t));
    fs->dev_id = dev_id;
    fs->start_lba = start_lba;

    /* Read boot sector */
    u8 buf[DISK_SECTOR_SIZE];
    if (disk_read_sector(start_lba, buf) != 0) { adv_free(fs); return -1; }

    struct fat_bpb* bpb = (struct fat_bpb*)buf;

    if (bpb->bytes_per_sec == 0) { adv_free(fs); return -1; }

    fs->bytes_per_sec = bpb->bytes_per_sec;
    fs->sec_per_clus = bpb->sec_per_clus;
    fs->cluster_size = bpb->bytes_per_sec * bpb->sec_per_clus;
    fs->fats = bpb->fats;
    fs->fat_start = bpb->reserved_sec;
    fs->dir_entries = bpb->root_entries;

    if (bpb->fat_sec_16) {
        fs->fat_length = bpb->fat_sec_16;
    } else {
        /* FAT32 - read from extended area */
        struct fat32_ext* ext = (struct fat32_ext*)(buf + sizeof(struct fat_bpb));
        fs->fat_length = ext->fat_sec_32;
        fs->root_cluster = ext->root_cluster;
    }

    if (fs->dir_entries) {
        /* FAT12/16: root dir is at a fixed location */
        fs->dir_start = fs->fat_start + (fs->fats * fs->fat_length);
        u32 root_dir_sectors = ((fs->dir_entries * sizeof(struct msdos_dir_entry))
                                + fs->bytes_per_sec - 1) / fs->bytes_per_sec;
        fs->data_start = fs->dir_start + root_dir_sectors;
    } else {
        /* FAT32: root dir is in the data area */
        fs->data_start = fs->fat_start + (fs->fats * fs->fat_length);
    }

    /* Determine FAT type based on cluster count */
    u32 data_sectors = 0;
    if (bpb->total_sec_16) {
        data_sectors = bpb->total_sec_16;
    } else {
        data_sectors = bpb->total_sec_32;
    }
    data_sectors -= fs->data_start;

    u32 total_clusters = data_sectors / fs->sec_per_clus;

    if (total_clusters < 4085)
        fs->fat_bits = 12;
    else if (total_clusters < 65525)
        fs->fat_bits = 16;
    else
        fs->fat_bits = 32;

    /* For FAT32, make sure root_cluster is set */
    if (fs->fat_bits == 32 && fs->root_cluster == 0) {
        struct fat32_ext* ext = (struct fat32_ext*)(buf + sizeof(struct fat_bpb));
        fs->root_cluster = ext->root_cluster;
        if (fs->root_cluster == 0) {
            fs->root_cluster = FAT_START_ENT;
        }
    }

    if (fs->root_cluster == 0) fs->root_cluster = FAT_START_ENT;

    *fs_data = fs;
    return 0;
}

int fat_umount(void* fs_data) {
    if (!fs_data) return -1;
    adv_free(fs_data);
    return 0;
}

int fat_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out) {
    if (!fs_data || !path || !out) return -1;
    fat_fs_t* fs = (fat_fs_t*)fs_data;

    struct msdos_dir_entry de;
    u32 cluster = fat_path_to_cluster(fs, path, &de);
    if (cluster == 0) return -1;

    if (str_cmp(path, "/") == 0) {
        /* Root directory - for FAT12/16, root is special */
        if (fs->fat_bits == 32) {
            return fat_dir_list(fs, fs->root_cluster, out, max_out);
        } else {
            /* FAT12/16 root directory is at a fixed location */
            u8 buf[8192];
            u32 root_sectors = ((fs->dir_entries * sizeof(struct msdos_dir_entry))
                                + fs->bytes_per_sec - 1) / fs->bytes_per_sec;
            if (root_sectors * DISK_SECTOR_SIZE > sizeof(buf)) root_sectors = sizeof(buf) / DISK_SECTOR_SIZE;
            for (u32 i = 0; i < root_sectors; i++) {
                if (fat_read_sectors(fs, fs->dir_start + i, buf + (i * DISK_SECTOR_SIZE), 1) != 0)
                    return -1;
            }

            u32 count = 0;
            u32 entries_per_sector = DISK_SECTOR_SIZE / sizeof(struct msdos_dir_entry);
            for (u32 s = 0; s < root_sectors && count < max_out; s++) {
                struct msdos_dir_entry* de_arr = (struct msdos_dir_entry*)(buf + (s * DISK_SECTOR_SIZE));
                for (u32 e = 0; e < entries_per_sector && count < max_out; e++) {
                    if (de_arr[e].name[0] == 0) return (int)count;
                    if (de_arr[e].name[0] == 0xE5) continue;
                    if (de_arr[e].attr == ATTR_VOLUME) continue;
                    if (de_arr[e].attr & ATTR_VOLUME) continue;

                    out[count].used = 1;
                    u32 start = (u32)de_arr[e].start | ((u32)de_arr[e].start_hi << 16);
                    out[count].id = start;
                    out[count].size = de_arr[e].size;
                    out[count].type = (de_arr[e].attr & ATTR_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
                    fat_name_to_str(de_arr[e].name, MSDOS_NAME, out[count].name, VFS_MAX_NAME);
                    count++;
                }
            }
            return (int)count;
        }
    }

    /* Check if it's a directory */
    if (!(de.attr & ATTR_DIR)) return -1;
    return fat_dir_list(fs, cluster, out, max_out);
}

int fat_read(void* fs_data, const char* path, char* out, u32 out_len) {
    if (!fs_data || !path || !out) return -1;
    fat_fs_t* fs = (fat_fs_t*)fs_data;

    struct msdos_dir_entry de;
    u32 cluster = fat_path_to_cluster(fs, path, &de);
    if (cluster == 0) return -1;
    if (de.attr & ATTR_DIR) return -1; /* Can't read directory */

    return fat_read_file(fs, cluster, out, out_len);
}

int fat_write(void* fs_data, const char* path, const char* data, u32 len) {
    (void)fs_data; (void)path; (void)data; (void)len;
    return -1;
}

int fat_create(void* fs_data, const char* path, u8 is_dir) {
    (void)fs_data; (void)path; (void)is_dir;
    return -1;
}

int fat_delete(void* fs_data, const char* path) {
    (void)fs_data; (void)path;
    return -1;
}

int fat_info(void* fs_data, const char* path, vfs_node_info_t* out) {
    if (!fs_data || !path || !out) return -1;
    fat_fs_t* fs = (fat_fs_t*)fs_data;

    struct msdos_dir_entry de;
    u32 cluster = fat_path_to_cluster(fs, path, &de);
    if (cluster == 0) return -1;

    fat_de_to_info(fs, &de, cluster, out);
    return 0;
}

int fat_format(u32 dev_id, u32 start_lba, u32 total_lba) {
    if (dev_id != 0) return -1;
    (void)disk_select(0);

    /* Create a FAT16 filesystem */
    if (total_lba < 2048) return -1;

    u32 bytes_per_sec = DISK_SECTOR_SIZE;
    u8 sec_per_clus = 1;
    if (total_lba > 65536) sec_per_clus = 2;
    if (total_lba > 131072) sec_per_clus = 4;
    if (total_lba > 262144) sec_per_clus = 8;
    if (total_lba > 524288) sec_per_clus = 16;

    u32 reserved_sec = 1; /* boot sector only */
    u8 fats = 2;
    u32 root_entries = 512;

    /* Calculate FAT size */
    u32 data_sectors = total_lba - reserved_sec - ((root_entries * 32 + bytes_per_sec - 1) / bytes_per_sec);
    u32 fat_sec_16 = (data_sectors * 2 + bytes_per_sec - 1) / bytes_per_sec; /* 2 bytes per FAT16 entry */

    /* Zero the boot sector area */
    u8 zero[DISK_SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    for (u32 i = 0; i < reserved_sec + (fats * fat_sec_16) + 1; i++) {
        disk_write_sector(start_lba + i, zero);
    }

    /* Write BPB */
    struct fat_bpb bpb;
    memset(&bpb, 0, sizeof(bpb));
    bpb.jmp_boot[0] = 0xEB;
    bpb.jmp_boot[1] = 0x3C;
    bpb.jmp_boot[2] = 0x90;
    memcpy(bpb.oem_name, "LASTOS   ", 8);
    bpb.bytes_per_sec = bytes_per_sec;
    bpb.sec_per_clus = sec_per_clus;
    bpb.reserved_sec = reserved_sec;
    bpb.fats = fats;
    bpb.root_entries = root_entries;
    bpb.total_sec_16 = (u16)(total_lba < 65536 ? total_lba : 0);
    bpb.media = 0xF8; /* hard disk */
    bpb.fat_sec_16 = (u16)fat_sec_16;
    bpb.sec_per_trk = 63;
    bpb.num_heads = 255;
    bpb.hidden_sec = 0;
    bpb.total_sec_32 = (total_lba >= 65536) ? total_lba : 0;

    u8 bpb_buf[DISK_SECTOR_SIZE];
    memset(bpb_buf, 0, sizeof(bpb_buf));
    memcpy(bpb_buf, &bpb, sizeof(bpb));
    /* Boot signature */
    bpb_buf[510] = 0x55;
    bpb_buf[511] = 0xAA;
    disk_write_sector(start_lba, bpb_buf);

    /* Write FATs */
    u8 fat_buf[DISK_SECTOR_SIZE];
    memset(fat_buf, 0, sizeof(fat_buf));
    /* FAT[0] = media ID + 0xFF, FAT[1] = EOF */
    fat_buf[0] = bpb.media;
    fat_buf[1] = 0xFF;
    fat_buf[2] = 0xFF;
    fat_buf[3] = 0xFF; /* FAT[1] = EOF marker */

    for (u8 f = 0; f < fats; f++) {
        for (u32 s = 0; s < fat_sec_16; s++) {
            u8 fbuf[DISK_SECTOR_SIZE];
            memset(fbuf, 0, sizeof(fbuf));
            if (s == 0) memcpy(fbuf, fat_buf, 4);
            disk_write_sector(start_lba + reserved_sec + (f * fat_sec_16) + s, fbuf);
        }
    }

    /* Root directory area is already zeroed */

    /* Write root directory entries (., ..) */
    {
        u8 root_sec[DISK_SECTOR_SIZE];
        memset(root_sec, 0, sizeof(root_sec));
        struct msdos_dir_entry* de_root = (struct msdos_dir_entry*)root_sec;
        memcpy(de_root[0].name, MSDOS_DOT, 11);
        de_root[0].attr = ATTR_DIR;
        de_root[0].start = 0;
        de_root[0].start_hi = 0;
        /* For FAT12/16, root dir cluster 0 is acceptable */
        memcpy(de_root[1].name, MSDOS_DOTDOT, 11);
        de_root[1].attr = ATTR_DIR;
        de_root[1].start = 0;
        de_root[1].start_hi = 0;

        u32 root_lba = start_lba + reserved_sec + (fats * fat_sec_16);
        disk_write_sector(root_lba, root_sec);
    }

    return 0;
}
