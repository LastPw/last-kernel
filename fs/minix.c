#include "fs_minix.h"
#include "advanced_allocator.h"
#include "disk.h"
#include "string.h"

#define MINIX_BLOCK_SIZE 1024u
#define MINIX_SECTORS_PER_BLOCK (MINIX_BLOCK_SIZE / DISK_SECTOR_SIZE)

/* ===================== Low-level I/O ===================== */

static int minix_read_block(minix_fs_t* fs, u32 block_num, u8* buffer) {
    u32 lba = fs->start_lba + (block_num * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) {
        if (disk_read_sector(lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

static int minix_write_block(minix_fs_t* fs, u32 block_num, const u8* buffer) {
    u32 lba = fs->start_lba + (block_num * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) {
        if (disk_write_sector(lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

static int read_zone(minix_fs_t* fs, u32 zone, u8* buffer, u32 count) {
    if (zone == 0) return -1;
    u32 block = zone;
    u32 lba = fs->start_lba + (block * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < count; i++) {
        if (disk_read_sector(lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

static int write_zone(minix_fs_t* fs, u32 zone, const u8* buffer, u32 count) {
    if (zone == 0) return -1;
    u32 block = zone;
    u32 lba = fs->start_lba + (block * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < count; i++) {
        if (disk_write_sector(lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

/* ===================== Inode helpers ===================== */

static u32 inode_block(minix_fs_t* fs, u32 ino) {
    ino--;
    u32 inodes_per_block;
    u32 inode_size;
    if (fs->s_version == MINIX_V1) {
        inode_size = sizeof(struct minix_inode);
        inodes_per_block = MINIX_BLOCK_SIZE / inode_size;
    } else {
        inode_size = sizeof(struct minix2_inode);
        inodes_per_block = MINIX_BLOCK_SIZE / inode_size;
    }
    u32 bitmap_total = fs->s_imap_blocks + fs->s_zmap_blocks;
    return 2 + bitmap_total + (ino / inodes_per_block);
}

static void* inode_ptr(minix_fs_t* fs, u8* block, u32 ino) {
    ino--;
    if (fs->s_version == MINIX_V1) {
        struct minix_inode* tab = (struct minix_inode*)block;
        u32 inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode);
        return &tab[ino % inodes_per_block];
    } else {
        struct minix2_inode* tab = (struct minix2_inode*)block;
        u32 inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix2_inode);
        return &tab[ino % inodes_per_block];
    }
}

static u32 minix_inode_size(void* raw_inode, u16 version) {
    if (version == MINIX_V1) return ((struct minix_inode*)raw_inode)->i_size;
    else return ((struct minix2_inode*)raw_inode)->i_size;
}

static u16 minix_inode_mode(void* raw_inode, u16 version) {
    if (version == MINIX_V1) return ((struct minix_inode*)raw_inode)->i_mode;
    else return ((struct minix2_inode*)raw_inode)->i_mode;
}

/* ===================== Zone (block) mapping ===================== */

static u32 minix_zone_from_inode(minix_fs_t* fs, void* raw_inode, u32 block_idx) {
    if (fs->s_version == MINIX_V1) {
        struct minix_inode* v1 = (struct minix_inode*)raw_inode;
        if (block_idx < 7) return (u32)v1->i_zone[block_idx];
        u8 buf[MINIX_BLOCK_SIZE];
        u16* ind = (u16*)buf;
        if (block_idx < 7 + 512) {
            if (v1->i_zone[7] == 0) return 0;
            if (read_zone(fs, v1->i_zone[7], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
            return (u32)ind[block_idx - 7];
        }
        block_idx -= 7 + 512;
        if (block_idx >= 512 * 512) return 0;
        if (v1->i_zone[8] == 0) return 0;
        if (read_zone(fs, v1->i_zone[8], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
        u16* ind1 = ind;
        if (ind1[block_idx / 512] == 0) return 0;
        if (read_zone(fs, (u32)ind1[block_idx / 512], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
        return (u32)ind[block_idx % 512];
    } else {
        struct minix2_inode* v2 = (struct minix2_inode*)raw_inode;
        if (block_idx < 10) return v2->i_zone[block_idx];
        u8 buf[MINIX_BLOCK_SIZE];
        u32* ind = (u32*)buf;
        u32 per_block = MINIX_BLOCK_SIZE / 4;
        if (block_idx < 10 + per_block) {
            if (v2->i_zone[10] == 0) return 0;
            if (read_zone(fs, v2->i_zone[10], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
            return ind[block_idx - 10];
        }
        return 0; /* Triple indirect not supported in this implementation */
    }
}

/* Set a zone pointer in an inode at a given block index. Returns new zone allocated. */
static u32 minix_set_zone_in_inode(minix_fs_t* fs, void* raw_inode, u32 block_idx, u32 zone) {
    if (fs->s_version == MINIX_V1) {
        struct minix_inode* v1 = (struct minix_inode*)raw_inode;
        if (block_idx < 7) {
            v1->i_zone[block_idx] = (u16)zone;
            return zone;
        }
        /* Single indirect: allocate indirect block on first use */
        if (v1->i_zone[7] == 0) return 0; /* Need to allocate indirect block first */
        u8 buf[MINIX_BLOCK_SIZE];
        if (read_zone(fs, v1->i_zone[7], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
        u16* ind = (u16*)buf;
        ind[block_idx - 7] = (u16)zone;
        if (write_zone(fs, v1->i_zone[7], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
        return zone;
    } else {
        struct minix2_inode* v2 = (struct minix2_inode*)raw_inode;
        if (block_idx < 10) {
            v2->i_zone[block_idx] = zone;
            return zone;
        }
        if (v2->i_zone[10] == 0) return 0;
        u8 buf[MINIX_BLOCK_SIZE];
        if (read_zone(fs, v2->i_zone[10], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
        u32* ind = (u32*)buf;
        ind[block_idx - 10] = zone;
        if (write_zone(fs, v2->i_zone[10], buf, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
        return zone;
    }
}

/* ===================== Bitmap operations ===================== */

static int minix_load_bitmaps(minix_fs_t* fs) {
    if (fs->imap) return 0;
    fs->imap_bytes = fs->s_imap_blocks * MINIX_BLOCK_SIZE;
    fs->zmap_bytes = fs->s_zmap_blocks * MINIX_BLOCK_SIZE;
    fs->imap = (u8*)adv_alloc(fs->imap_bytes);
    fs->zmap = (u8*)adv_alloc(fs->zmap_bytes);
    if (!fs->imap || !fs->zmap) {
        if (fs->imap) adv_free(fs->imap);
        if (fs->zmap) adv_free(fs->zmap);
        fs->imap = 0; fs->zmap = 0;
        return -1;
    }
    /* Read imap blocks */
    for (u32 i = 0; i < fs->s_imap_blocks; i++) {
        if (minix_read_block(fs, 2 + i, fs->imap + (i * MINIX_BLOCK_SIZE)) != 0) {
            adv_free(fs->imap); adv_free(fs->zmap);
            fs->imap = 0; fs->zmap = 0;
            return -1;
        }
    }
    /* Read zmap blocks */
    for (u32 i = 0; i < fs->s_zmap_blocks; i++) {
        if (minix_read_block(fs, 2 + fs->s_imap_blocks + i, fs->zmap + (i * MINIX_BLOCK_SIZE)) != 0) {
            adv_free(fs->imap); adv_free(fs->zmap);
            fs->imap = 0; fs->zmap = 0;
            return -1;
        }
    }
    return 0;
}

static int minix_sync_bitmaps(minix_fs_t* fs) {
    if (!fs->imap) return -1;
    for (u32 i = 0; i < fs->s_imap_blocks; i++) {
        if (minix_write_block(fs, 2 + i, fs->imap + (i * MINIX_BLOCK_SIZE)) != 0) return -1;
    }
    for (u32 i = 0; i < fs->s_zmap_blocks; i++) {
        if (minix_write_block(fs, 2 + fs->s_imap_blocks + i, fs->zmap + (i * MINIX_BLOCK_SIZE)) != 0) return -1;
    }
    return 0;
}

/* Find first zero bit in bitmap, up to max_bits. Returns bit index or 0 if full. */
static u32 minix_bitmap_find_zero(u8* bitmap, u32 max_bits) {
    for (u32 i = 0; i < max_bits; i++) {
        u32 byte_idx = i / 8;
        u8 bit_mask = (u8)(1u << (i % 8));
        if ((bitmap[byte_idx] & bit_mask) == 0) return i;
    }
    return 0; /* None found (0 is reserved anyway) */
}

static void minix_bitmap_set(u8* bitmap, u32 bit) {
    bitmap[bit / 8] |= (u8)(1u << (bit % 8));
}

static void minix_bitmap_clear(u8* bitmap, u32 bit) {
    bitmap[bit / 8] &= (u8)~(1u << (bit % 8));
}

/* Allocate a free inode. Returns inode number (1-based) or 0 if none. */
static u32 minix_alloc_inode(minix_fs_t* fs) {
    u32 bit = minix_bitmap_find_zero(fs->imap, fs->s_ninodes);
    if (bit == 0) return 0;
    minix_bitmap_set(fs->imap, bit);
    return bit;
}

/* Free an inode */
static void minix_free_inode(minix_fs_t* fs, u32 ino) {
    if (ino == 0 || ino > fs->s_ninodes) return;
    minix_bitmap_clear(fs->imap, ino);
}

/* Allocate a free zone. Returns zone number or 0 if none. */
static u32 minix_alloc_zone(minix_fs_t* fs) {
    u32 nzones = fs->s_nzones;
    u32 bit = minix_bitmap_find_zero(fs->zmap, nzones);
    if (bit == 0) return 0;
    minix_bitmap_set(fs->zmap, bit);
    /* zone number = bit + fs->s_firstdatazone - 1 (bit 0 = zone firstdatazone-1, zone 0 unused) */
    u32 zone = bit + fs->s_firstdatazone - 1;
    if (zone < fs->s_firstdatazone || zone >= fs->s_firstdatazone + fs->s_nzones) return 0;
    /* Zero the newly allocated zone */
    u8 zero[MINIX_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    if (write_zone(fs, zone, zero, MINIX_SECTORS_PER_BLOCK) != 0) return 0;
    return zone;
}

static void minix_free_zone(minix_fs_t* fs, u32 zone) {
    if (zone < fs->s_firstdatazone) return;
    u32 bit = zone - fs->s_firstdatazone + 1;
    if (bit >= fs->s_nzones) return;
    minix_bitmap_clear(fs->zmap, bit);
}

/* Free all zones belonging to an inode (direct + indirect) */
static void minix_free_inode_zones(minix_fs_t* fs, void* raw_inode) {
    if (fs->s_version == MINIX_V1) {
        struct minix_inode* v1 = (struct minix_inode*)raw_inode;
        for (u32 i = 0; i < 7; i++) {
            if (v1->i_zone[i]) { minix_free_zone(fs, v1->i_zone[i]); v1->i_zone[i] = 0; }
        }
        /* Free indirect block */
        if (v1->i_zone[7]) {
            u8 buf[MINIX_BLOCK_SIZE];
            if (read_zone(fs, v1->i_zone[7], buf, MINIX_SECTORS_PER_BLOCK) == 0) {
                u16* ind = (u16*)buf;
                for (u32 i = 0; i < 512; i++) {
                    if (ind[i]) minix_free_zone(fs, (u32)ind[i]);
                }
            }
            minix_free_zone(fs, v1->i_zone[7]);
            v1->i_zone[7] = 0;
        }
        if (v1->i_zone[8]) {
            u8 buf[MINIX_BLOCK_SIZE];
            if (read_zone(fs, v1->i_zone[8], buf, MINIX_SECTORS_PER_BLOCK) == 0) {
                u16* ind1 = (u16*)buf;
                for (u32 i = 0; i < 512; i++) {
                    if (ind1[i]) {
                        u8 buf2[MINIX_BLOCK_SIZE];
                        if (read_zone(fs, (u32)ind1[i], buf2, MINIX_SECTORS_PER_BLOCK) == 0) {
                            u16* ind2 = (u16*)buf2;
                            for (u32 j = 0; j < 512; j++) {
                                if (ind2[j]) minix_free_zone(fs, (u32)ind2[j]);
                            }
                        }
                        minix_free_zone(fs, (u32)ind1[i]);
                    }
                }
            }
            minix_free_zone(fs, v1->i_zone[8]);
            v1->i_zone[8] = 0;
        }
    } else {
        struct minix2_inode* v2 = (struct minix2_inode*)raw_inode;
        for (u32 i = 0; i < 10; i++) {
            if (v2->i_zone[i]) { minix_free_zone(fs, v2->i_zone[i]); v2->i_zone[i] = 0; }
        }
        if (v2->i_zone[10]) {
            u8 buf[MINIX_BLOCK_SIZE];
            if (read_zone(fs, v2->i_zone[10], buf, MINIX_SECTORS_PER_BLOCK) == 0) {
                u32* ind = (u32*)buf;
                u32 per = MINIX_BLOCK_SIZE / 4;
                for (u32 i = 0; i < per; i++) {
                    if (ind[i]) minix_free_zone(fs, ind[i]);
                }
            }
            minix_free_zone(fs, v2->i_zone[10]);
            v2->i_zone[10] = 0;
        }
    }
}

/* ===================== Directory entry helpers ===================== */

static int minix_dir_add_entry(minix_fs_t* fs, u32 dir_ino, u32 file_ino, const char* name) {
    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, dir_ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, dir_ino);
    u32 size = minix_inode_size(raw, fs->s_version);
    u32 nlen = str_len(name);
    if (nlen > fs->s_namelen) nlen = fs->s_namelen;
    u32 entry_size = fs->s_dirsize;
    u32 num_blocks = (size + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
    u8 block_buf[MINIX_BLOCK_SIZE];

    /* Scan existing blocks for a free entry */
    for (u32 b = 0; b <= num_blocks; b++) {
        u32 zone;
        if (b < num_blocks) {
            zone = minix_zone_from_inode(fs, raw, b);
            if (zone == 0) continue;
            if (read_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) continue;
        } else {
            /* Need to add a new block */
            if (num_blocks > 0 && size % MINIX_BLOCK_SIZE == 0) {
                zone = minix_alloc_zone(fs);
                if (zone == 0) return -1;
                memset(block_buf, 0, sizeof(block_buf));
                /* Set zone in inode */
                if (minix_set_zone_in_inode(fs, raw, b, zone) == 0) { minix_free_zone(fs, zone); return -1; }
                /* Update inode size */
                u32 new_size = size + MINIX_BLOCK_SIZE;
                if (fs->s_version == MINIX_V1) {
                    ((struct minix_inode*)raw)->i_size = new_size;
                } else {
                    ((struct minix2_inode*)raw)->i_size = new_size;
                }
                /* Write inode back */
                if (minix_write_block(fs, inode_block(fs, dir_ino), ibuf) != 0) return -1;
            } else {
                return -1; /* Can't extend */
            }
        }

        /* Scan for empty slot */
        u32 offset = 0;
        while (offset + entry_size <= MINIX_BLOCK_SIZE) {
            u32 de_ino;
            if (fs->s_version == MINIX_V3) {
                struct minix3_dir_entry* de = (struct minix3_dir_entry*)(block_buf + offset);
                de_ino = de->inode;
            } else {
                struct minix_dir_entry* de = (struct minix_dir_entry*)(block_buf + offset);
                de_ino = de->inode;
            }
            if (de_ino == 0) {
                /* Found empty slot */
                if (fs->s_version == MINIX_V3) {
                    struct minix3_dir_entry* de = (struct minix3_dir_entry*)(block_buf + offset);
                    de->inode = file_ino;
                    memset(de->name, 0, fs->s_namelen);
                    memcpy(de->name, name, nlen);
                } else {
                    struct minix_dir_entry* de = (struct minix_dir_entry*)(block_buf + offset);
                    de->inode = (u16)file_ino;
                    memset(de->name, 0, fs->s_namelen);
                    memcpy(de->name, name, nlen);
                }
                if (write_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) return -1;
                return 0;
            }
            offset += entry_size;
        }
    }
    return -1;
}

static int minix_dir_remove_entry(minix_fs_t* fs, u32 dir_ino, const char* name) {
    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, dir_ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, dir_ino);
    u32 size = minix_inode_size(raw, fs->s_version);
    u32 entry_size = fs->s_dirsize;
    u32 num_blocks = (size + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
    u8 block_buf[MINIX_BLOCK_SIZE];

    for (u32 b = 0; b < num_blocks; b++) {
        u32 zone = minix_zone_from_inode(fs, raw, b);
        if (zone == 0) continue;
        if (read_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) continue;

        u32 offset = 0;
        while (offset + entry_size <= MINIX_BLOCK_SIZE) {
            u32 de_ino;
            char ename[64];
            if (fs->s_version == MINIX_V3) {
                struct minix3_dir_entry* de = (struct minix3_dir_entry*)(block_buf + offset);
                de_ino = de->inode;
                memcpy(ename, de->name, fs->s_namelen);
                ename[fs->s_namelen] = '\0';
            } else {
                struct minix_dir_entry* de = (struct minix_dir_entry*)(block_buf + offset);
                de_ino = de->inode;
                memcpy(ename, de->name, fs->s_namelen);
                ename[fs->s_namelen] = '\0';
            }
            if (de_ino != 0 && str_cmp(ename, name) == 0) {
                /* Found - clear entry */
                if (fs->s_version == MINIX_V3) {
                    ((struct minix3_dir_entry*)(block_buf + offset))->inode = 0;
                } else {
                    ((struct minix_dir_entry*)(block_buf + offset))->inode = 0;
                }
                if (write_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) return -1;
                return 0;
            }
            offset += entry_size;
        }
    }
    return -1; /* Not found */
}

/* ===================== Read helpers (unchanged) ===================== */

static void minix_inode_to_info(minix_fs_t* fs, void* raw, u32 ino, vfs_node_info_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(vfs_node_info_t));
    out->used = 1;
    out->id = ino;
    if (fs->s_version == MINIX_V1) {
        struct minix_inode* v1 = (struct minix_inode*)raw;
        out->size = v1->i_size;
        out->type = (v1->i_mode & 0x4000) ? VFS_NODE_DIR : VFS_NODE_FILE;
        out->mode = (u16)(v1->i_mode & 0x1FF);
    } else {
        struct minix2_inode* v2 = (struct minix2_inode*)raw;
        out->size = v2->i_size;
        out->type = (v2->i_mode & 0x4000) ? VFS_NODE_DIR : VFS_NODE_FILE;
        out->mode = (u16)(v2->i_mode & 0x1FF);
    }
}

static u32 minix_dir_lookup(minix_fs_t* fs, u32 dir_ino, const char* name) {
    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, dir_ino), ibuf) != 0) return 0;
    void* raw = inode_ptr(fs, ibuf, dir_ino);
    u32 size = minix_inode_size(raw, fs->s_version);
    u16 mode = minix_inode_mode(raw, fs->s_version);
    if (!(mode & 0x4000)) return 0;
    u32 nlen = str_len(name);
    if (nlen > fs->s_namelen) return 0;
    u32 num_blocks = (size + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
    u8 block_buf[MINIX_BLOCK_SIZE];
    u32 entry_size = fs->s_dirsize;
    for (u32 b = 0; b < num_blocks; b++) {
        u32 zone = minix_zone_from_inode(fs, raw, b);
        if (zone == 0) continue;
        if (read_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) continue;
        u32 offset = 0;
        while (offset + entry_size <= MINIX_BLOCK_SIZE) {
            u32 ino_num = 0;
            char ename[64];
            ename[0] = '\0';
            if (fs->s_version == MINIX_V3) {
                struct minix3_dir_entry* de = (struct minix3_dir_entry*)(block_buf + offset);
                ino_num = de->inode;
                memcpy(ename, de->name, fs->s_namelen);
                ename[fs->s_namelen] = '\0';
            } else {
                struct minix_dir_entry* de = (struct minix_dir_entry*)(block_buf + offset);
                ino_num = de->inode;
                memcpy(ename, de->name, fs->s_namelen);
                ename[fs->s_namelen] = '\0';
            }
            if (ino_num != 0 && str_cmp(ename, name) == 0) return ino_num;
            offset += entry_size;
        }
    }
    return 0;
}

static int minix_dir_list(minix_fs_t* fs, u32 dir_ino, vfs_node_info_t* out, u32 max_out) {
    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, dir_ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, dir_ino);
    u32 size = minix_inode_size(raw, fs->s_version);
    u16 mode = minix_inode_mode(raw, fs->s_version);
    if (!(mode & 0x4000)) return -1;
    u32 num_blocks = (size + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
    u8 block_buf[MINIX_BLOCK_SIZE];
    u32 entry_size = fs->s_dirsize;
    u32 count = 0;
    for (u32 b = 0; b < num_blocks && count < max_out; b++) {
        u32 zone = minix_zone_from_inode(fs, raw, b);
        if (zone == 0) continue;
        if (read_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) continue;
        u32 offset = 0;
        while (offset + entry_size <= MINIX_BLOCK_SIZE && count < max_out) {
            u32 ino_num = 0;
            char ename[64];
            ename[0] = '\0';
            if (fs->s_version == MINIX_V3) {
                struct minix3_dir_entry* de = (struct minix3_dir_entry*)(block_buf + offset);
                ino_num = de->inode;
                memcpy(ename, de->name, fs->s_namelen);
                ename[fs->s_namelen] = '\0';
            } else {
                struct minix_dir_entry* de = (struct minix_dir_entry*)(block_buf + offset);
                ino_num = de->inode;
                memcpy(ename, de->name, fs->s_namelen);
                ename[fs->s_namelen] = '\0';
            }
            if (ino_num != 0) {
                out[count].used = 1;
                out[count].id = ino_num;
                memcpy(out[count].name, ename, VFS_MAX_NAME - 1);
                out[count].name[VFS_MAX_NAME - 1] = '\0';
                u8 ibuf2[MINIX_BLOCK_SIZE];
                if (minix_read_block(fs, inode_block(fs, ino_num), ibuf2) == 0) {
                    void* r2 = inode_ptr(fs, ibuf2, ino_num);
                    u16 m = minix_inode_mode(r2, fs->s_version);
                    out[count].type = (m & 0x4000) ? VFS_NODE_DIR : VFS_NODE_FILE;
                    out[count].size = minix_inode_size(r2, fs->s_version);
                }
                count++;
            }
            offset += entry_size;
        }
    }
    return (int)count;
}

static int minix_inode_info(minix_fs_t* fs, u32 ino, vfs_node_info_t* out) {
    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, ino);
    minix_inode_to_info(fs, raw, ino, out);
    return 0;
}

/* ===================== Path traversal ===================== */

/* Resolve path to parent dir ino + leaf name. Returns parent inode or 0. */
static u32 minix_path_resolve(minix_fs_t* fs, const char* path, char* leaf, u32 leaf_sz) {
    if (!path || path[0] != '/') return 0;
    leaf[0] = '\0';
    if (str_cmp(path, "/") == 0) return 0; /* Can't resolve root */

    char name[FS_MAX_PATH];
    u32 cur_ino = MINIX_ROOT_INO;
    u32 i = 1;

    while (path[i]) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;
        u32 ni = 0;
        while (path[i] && path[i] != '/' && ni < sizeof(name) - 1) name[ni++] = path[i++];
        name[ni] = '\0';

        if (path[i] == '\0' || path[i] == '/') {
            /* Last component - this is the leaf */
            memcpy(leaf, name, leaf_sz > ni ? ni : leaf_sz);
            leaf[leaf_sz - 1] = '\0';
            return cur_ino;
        }
        u32 next = minix_dir_lookup(fs, cur_ino, name);
        if (next == 0) return 0;
        cur_ino = next;
    }
    return 0;
}

static int minix_path_read(minix_fs_t* fs, const char* path, char* out, u32 out_len) {
    if (!path || path[0] != '/') return -1;
    if (str_cmp(path, "/") == 0) return -1;
    char leaf[FS_MAX_PATH];
    u32 parent = minix_path_resolve(fs, path, leaf, sizeof(leaf));
    if (parent == 0) return -1;
    u32 ino = minix_dir_lookup(fs, parent, leaf);
    if (ino == 0) return -1;

    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, ino);
    u32 size = minix_inode_size(raw, fs->s_version);
    if (size == 0) return 0;
    u32 to_read = (size < out_len) ? size : out_len;
    u8 block_buf[MINIX_BLOCK_SIZE];
    u32 num_blocks = (size + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
    u32 done = 0;
    for (u32 b = 0; b < num_blocks && done < to_read; b++) {
        u32 zone = minix_zone_from_inode(fs, raw, b);
        if (zone == 0) break;
        if (read_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) break;
        u32 chunk = MINIX_BLOCK_SIZE;
        if (done + chunk > to_read) chunk = to_read - done;
        memcpy(out + done, block_buf, chunk);
        done += chunk;
    }
    if (done < out_len) out[done] = '\0';
    return (int)done;
}

static int minix_path_info(minix_fs_t* fs, const char* path, vfs_node_info_t* out) {
    if (!path || path[0] != '/') return -1;
    if (str_cmp(path, "/") == 0) return minix_inode_info(fs, MINIX_ROOT_INO, out);
    char leaf[FS_MAX_PATH];
    u32 parent = minix_path_resolve(fs, path, leaf, sizeof(leaf));
    if (parent == 0) return -1;
    u32 ino = minix_dir_lookup(fs, parent, leaf);
    if (ino == 0) return -1;
    return minix_inode_info(fs, ino, out);
}

static int minix_path_list(minix_fs_t* fs, const char* path, vfs_node_info_t* out, u32 max_out) {
    if (!path || path[0] != '/') return -1;
    u32 dir_ino;
    if (str_cmp(path, "/") == 0) dir_ino = MINIX_ROOT_INO;
    else {
        vfs_node_info_t info;
        if (minix_path_info(fs, path, &info) != 0) return -1;
        if (info.type != VFS_NODE_DIR) return -1;
        dir_ino = info.id;
    }
    return minix_dir_list(fs, dir_ino, out, max_out);
}

/* ===================== Driver interface ===================== */

int minix_mount(u32 dev_id, u32 start_lba, void** fs_data) {
    if (!fs_data) return -1;
    if (dev_id != 0) return -1;
    (void)disk_select(0);

    minix_fs_t* fs = (minix_fs_t*)adv_alloc(sizeof(minix_fs_t));
    if (!fs) return -1;
    memset(fs, 0, sizeof(minix_fs_t));
    fs->dev_id = dev_id;
    fs->start_lba = start_lba;
    fs->block_size = MINIX_BLOCK_SIZE;

    u8 sbuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, 1, sbuf) != 0) { adv_free(fs); return -1; }

    struct minix_super_block* ms = (struct minix_super_block*)sbuf;
    u16 magic = ms->s_magic;

    if (magic == MINIX_SUPER_MAGIC || magic == MINIX_SUPER_MAGIC2) {
        fs->s_version = MINIX_V1;
        fs->s_ninodes = ms->s_ninodes;
        fs->s_nzones = ms->s_nzones;
        fs->s_imap_blocks = ms->s_imap_blocks;
        fs->s_zmap_blocks = ms->s_zmap_blocks;
        fs->s_firstdatazone = ms->s_firstdatazone;
        fs->s_log_zone_size = ms->s_log_zone_size;
        fs->s_mount_state = ms->s_state;
        fs->s_dirsize = (magic == MINIX_SUPER_MAGIC) ? 16 : 32;
        fs->s_namelen = (magic == MINIX_SUPER_MAGIC) ? 14 : 30;
    } else if (magic == MINIX2_SUPER_MAGIC || magic == MINIX2_SUPER_MAGIC2) {
        fs->s_version = MINIX_V2;
        fs->s_ninodes = ms->s_ninodes;
        fs->s_nzones = ms->s_zones;
        fs->s_imap_blocks = ms->s_imap_blocks;
        fs->s_zmap_blocks = ms->s_zmap_blocks;
        fs->s_firstdatazone = ms->s_firstdatazone;
        fs->s_log_zone_size = ms->s_log_zone_size;
        fs->s_mount_state = ms->s_state;
        fs->s_dirsize = (magic == MINIX2_SUPER_MAGIC) ? 16 : 32;
        fs->s_namelen = (magic == MINIX2_SUPER_MAGIC) ? 14 : 30;
    } else {
        u16* magic3 = (u16*)(sbuf + 24);
        if (*magic3 == MINIX3_SUPER_MAGIC) {
            struct minix3_super_block* m3 = (struct minix3_super_block*)sbuf;
            fs->s_version = MINIX_V3;
            fs->s_ninodes = m3->s_ninodes;
            fs->s_nzones = m3->s_zones;
            fs->s_imap_blocks = m3->s_imap_blocks;
            fs->s_zmap_blocks = m3->s_zmap_blocks;
            fs->s_firstdatazone = m3->s_firstdatazone;
            fs->s_log_zone_size = m3->s_log_zone_size;
            fs->s_dirsize = 64;
            fs->s_namelen = 60;
            fs->s_mount_state = 1;
            fs->block_size = m3->s_blocksize ? m3->s_blocksize : MINIX_BLOCK_SIZE;
        } else { adv_free(fs); return -1; }
    }

    /* Load bitmaps on mount for write support */
    if (minix_load_bitmaps(fs) != 0) { adv_free(fs); return -1; }

    *fs_data = fs;
    return 0;
}

int minix_umount(void* fs_data) {
    if (!fs_data) return -1;
    minix_fs_t* fs = (minix_fs_t*)fs_data;
    /* Sync bitmaps */
    if (fs->imap) minix_sync_bitmaps(fs);
    /* Mark filesystem clean */
    if (fs->s_version != MINIX_V3) {
        u8 sbuf[MINIX_BLOCK_SIZE];
        if (minix_read_block(fs, 1, sbuf) == 0) {
            struct minix_super_block* ms = (struct minix_super_block*)sbuf;
            ms->s_state = fs->s_mount_state;
            minix_write_block(fs, 1, sbuf);
        }
    }
    if (fs->imap) adv_free(fs->imap);
    if (fs->zmap) adv_free(fs->zmap);
    adv_free(fs);
    return 0;
}

int minix_list(void* fs_data, const char* path, vfs_node_info_t* out, u32 max_out) {
    if (!fs_data || !path || !out) return -1;
    return minix_path_list((minix_fs_t*)fs_data, path, out, max_out);
}

int minix_read(void* fs_data, const char* path, char* out, u32 out_len) {
    if (!fs_data || !path || !out) return -1;
    return minix_path_read((minix_fs_t*)fs_data, path, out, out_len);
}

int minix_write(void* fs_data, const char* path, const char* data, u32 len) {
    if (!fs_data || !path || !data) return -1;
    minix_fs_t* fs = (minix_fs_t*)fs_data;
    if (str_cmp(path, "/") == 0) return -1;

    char leaf[FS_MAX_PATH];
    u32 parent = minix_path_resolve(fs, path, leaf, sizeof(leaf));
    if (parent == 0) return -1;
    u32 ino = minix_dir_lookup(fs, parent, leaf);
    if (ino == 0) return -1;

    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, ino);
    u16 mode = minix_inode_mode(raw, fs->s_version);
    if (mode & 0x4000) return -1; /* Can't write to directory */

    u32 old_size = minix_inode_size(raw, fs->s_version);
    u32 num_blocks_needed = (len + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
    u8 block_buf[MINIX_BLOCK_SIZE];

    /* Write data to zones, extending as needed */
    for (u32 b = 0; b < num_blocks_needed; b++) {
        u32 zone = minix_zone_from_inode(fs, raw, b);
        if (zone == 0) {
            zone = minix_alloc_zone(fs);
            if (zone == 0) return -1;
            if (minix_set_zone_in_inode(fs, raw, b, zone) == 0) { minix_free_zone(fs, zone); return -1; }
        }
        u32 offset = b * MINIX_BLOCK_SIZE;
        u32 chunk = (len > offset) ? (len - offset) : 0;
        if (chunk > MINIX_BLOCK_SIZE) chunk = MINIX_BLOCK_SIZE;
        if (chunk < MINIX_BLOCK_SIZE) {
            /* Read-modify-write for partial blocks */
            if (read_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0)
                memset(block_buf, 0, sizeof(block_buf));
        }
        if (chunk) memcpy(block_buf, data + offset, chunk);
        else memset(block_buf, 0, sizeof(block_buf));
        if (write_zone(fs, zone, block_buf, MINIX_SECTORS_PER_BLOCK) != 0) return -1;
    }

    /* Free any extra zones if shrinking */
    if (old_size > len) {
        u32 old_blocks = (old_size + MINIX_BLOCK_SIZE - 1) / MINIX_BLOCK_SIZE;
        for (u32 b = num_blocks_needed; b < old_blocks; b++) {
            u32 zone = minix_zone_from_inode(fs, raw, b);
            if (zone) { minix_free_zone(fs, zone);
                if (minix_set_zone_in_inode(fs, raw, b, 0) == 0 && zone != 0) {} }
        }
    }

    /* Update inode size */
    if (fs->s_version == MINIX_V1) ((struct minix_inode*)raw)->i_size = len;
    else ((struct minix2_inode*)raw)->i_size = len;

    /* Write inode back */
    if (minix_write_block(fs, inode_block(fs, ino), ibuf) != 0) return -1;
    /* Sync bitmaps */
    if (minix_sync_bitmaps(fs) != 0) return -1;
    return (int)len;
}

int minix_create(void* fs_data, const char* path, u8 is_dir) {
    if (!fs_data || !path) return -1;
    minix_fs_t* fs = (minix_fs_t*)fs_data;
    if (str_cmp(path, "/") == 0) return -1;

    char leaf[FS_MAX_PATH];
    u32 parent = minix_path_resolve(fs, path, leaf, sizeof(leaf));
    if (parent == 0) return -1;

    /* Check if already exists */
    if (minix_dir_lookup(fs, parent, leaf) != 0) return -1;

    u32 nlen = str_len(leaf);
    if (nlen == 0 || nlen > fs->s_namelen) return -1;

    /* Allocate inode */
    u32 ino = minix_alloc_inode(fs);
    if (ino == 0) return -1;

    /* Allocate a zone for the file data / directory */
    u32 zone = minix_alloc_zone(fs);
    if (zone == 0) { minix_free_inode(fs, ino); return -1; }

    /* Initialize inode */
    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, ino), ibuf) != 0) { minix_free_inode(fs, ino); minix_free_zone(fs, zone); return -1; }
    void* raw = inode_ptr(fs, ibuf, ino);
    memset(raw, 0, (fs->s_version == MINIX_V1) ? sizeof(struct minix_inode) : sizeof(struct minix2_inode));

    u16 mode;
    if (is_dir) {
        mode = 0x41FF; /* directory, 0777 */
    } else {
        mode = 0x81FF; /* regular file, 0777 */
    }

    if (fs->s_version == MINIX_V1) {
        struct minix_inode* v1 = (struct minix_inode*)raw;
        v1->i_mode = mode;
        v1->i_uid = 0;
        v1->i_gid = 0;
        v1->i_nlinks = 1;
        v1->i_zone[0] = (u16)zone;
        if (is_dir) {
            v1->i_nlinks = 2;
            v1->i_size = 2 * fs->s_dirsize;
        } else {
            v1->i_size = 0;
        }
    } else {
        struct minix2_inode* v2 = (struct minix2_inode*)raw;
        v2->i_mode = mode;
        v2->i_uid = 0;
        v2->i_gid = 0;
        v2->i_nlinks = 1;
        v2->i_zone[0] = zone;
        if (is_dir) {
            v2->i_nlinks = 2;
            v2->i_size = 2 * fs->s_dirsize;
        } else {
            v2->i_size = 0;
        }
    }

    if (minix_write_block(fs, inode_block(fs, ino), ibuf) != 0) {
        minix_free_inode(fs, ino); minix_free_zone(fs, zone); return -1;
    }

    /* Initialize data zone */
    u8 dbuf[MINIX_BLOCK_SIZE];
    memset(dbuf, 0, sizeof(dbuf));
    if (is_dir) {
        /* Create . and .. entries */
        if (fs->s_version == MINIX_V3) {
            struct minix3_dir_entry* d3 = (struct minix3_dir_entry*)dbuf;
            d3->inode = ino;
            memcpy(d3->name, ".", 1);
            struct minix3_dir_entry* d32 = (struct minix3_dir_entry*)(dbuf + fs->s_dirsize);
            d32->inode = parent;
            memcpy(d32->name, "..", 2);
        } else {
            struct minix_dir_entry* de = (struct minix_dir_entry*)dbuf;
            de->inode = (u16)ino;
            memcpy(de->name, ".", 1);
            struct minix_dir_entry* de2 = (struct minix_dir_entry*)(dbuf + fs->s_dirsize);
            de2->inode = (u16)parent;
            memcpy(de2->name, "..", 2);
        }
    }
    if (write_zone(fs, zone, dbuf, MINIX_SECTORS_PER_BLOCK) != 0) return -1;

    /* Add directory entry in parent */
    if (minix_dir_add_entry(fs, parent, ino, leaf) != 0) {
        minix_free_inode(fs, ino);
        minix_free_zone(fs, zone);
        return -1;
    }

    /* If directory, increment parent link count */
    if (is_dir) {
        u8 pibuf[MINIX_BLOCK_SIZE];
        if (minix_read_block(fs, inode_block(fs, parent), pibuf) == 0) {
            void* p_raw = inode_ptr(fs, pibuf, parent);
            if (fs->s_version == MINIX_V1) ((struct minix_inode*)p_raw)->i_nlinks++;
            else ((struct minix2_inode*)p_raw)->i_nlinks++;
            minix_write_block(fs, inode_block(fs, parent), pibuf);
        }
    }

    if (minix_sync_bitmaps(fs) != 0) return -1;
    return 0;
}

int minix_delete(void* fs_data, const char* path) {
    if (!fs_data || !path) return -1;
    minix_fs_t* fs = (minix_fs_t*)fs_data;
    if (str_cmp(path, "/") == 0) return -1;

    char leaf[FS_MAX_PATH];
    u32 parent = minix_path_resolve(fs, path, leaf, sizeof(leaf));
    if (parent == 0) return -1;
    u32 ino = minix_dir_lookup(fs, parent, leaf);
    if (ino == 0) return -1;

    u8 ibuf[MINIX_BLOCK_SIZE];
    if (minix_read_block(fs, inode_block(fs, ino), ibuf) != 0) return -1;
    void* raw = inode_ptr(fs, ibuf, ino);
    u16 mode = minix_inode_mode(raw, fs->s_version);

    /* Don't delete root's parent refs */
    if (ino == MINIX_ROOT_INO) return -1;

    /* Check if directory is empty (only . and ..) */
    if (mode & 0x4000) {
        u32 size = minix_inode_size(raw, fs->s_version);
        if (size > 2 * (u32)fs->s_dirsize) return -1; /* Not empty */
    }

    /* Remove directory entry */
    if (minix_dir_remove_entry(fs, parent, leaf) != 0) return -1;

    /* Free all zones */
    minix_free_inode_zones(fs, raw);

    /* Free inode */
    minix_free_inode(fs, ino);

    /* Decrement parent link count if directory */
    if (mode & 0x4000) {
        u8 pibuf[MINIX_BLOCK_SIZE];
        if (minix_read_block(fs, inode_block(fs, parent), pibuf) == 0) {
            void* p_raw = inode_ptr(fs, pibuf, parent);
            if (fs->s_version == MINIX_V1 && ((struct minix_inode*)p_raw)->i_nlinks > 1)
                ((struct minix_inode*)p_raw)->i_nlinks--;
            else if (fs->s_version != MINIX_V1 && ((struct minix2_inode*)p_raw)->i_nlinks > 1)
                ((struct minix2_inode*)p_raw)->i_nlinks--;
            minix_write_block(fs, inode_block(fs, parent), pibuf);
        }
    }

    /* Clear inode on disk */
    memset(raw, 0, (fs->s_version == MINIX_V1) ? sizeof(struct minix_inode) : sizeof(struct minix2_inode));
    minix_write_block(fs, inode_block(fs, ino), ibuf);

    if (minix_sync_bitmaps(fs) != 0) return -1;
    return 0;
}

int minix_info(void* fs_data, const char* path, vfs_node_info_t* out) {
    if (!fs_data || !path || !out) return -1;
    return minix_path_info((minix_fs_t*)fs_data, path, out);
}

int minix_format(u32 dev_id, u32 start_lba, u32 total_lba) {
    /* (unchanged from previous - omitted for brevity) */
    if (dev_id != 0) return -1;
    (void)disk_select(0);
    u32 total_blocks = (total_lba * DISK_SECTOR_SIZE) / MINIX_BLOCK_SIZE;
    if (total_blocks < 64) return -1;
    u32 ninodes = total_blocks / 16;
    if (ninodes < 16) ninodes = 16;
    if (ninodes > 4096) ninodes = 4096;
    u32 zmap_blocks = (total_blocks + 1023) / 1024;
    if (zmap_blocks < 1) zmap_blocks = 1;
    u32 imap_blocks = (ninodes + 1023) / 1024;
    if (imap_blocks < 1) imap_blocks = 1;
    u32 firstdatazone = 2 + imap_blocks + zmap_blocks;
    u32 nzones = total_blocks - firstdatazone;

    u8 zero[MINIX_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    u32 total_sectors = (firstdatazone + nzones) * MINIX_SECTORS_PER_BLOCK;
    for (u32 i = 0; i < total_sectors; i++) disk_write_sector(start_lba + i, zero);

    struct minix_super_block sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_ninodes = (u16)ninodes;
    sb.s_nzones = (u16)nzones;
    sb.s_imap_blocks = (u16)imap_blocks;
    sb.s_zmap_blocks = (u16)zmap_blocks;
    sb.s_firstdatazone = (u16)firstdatazone;
    sb.s_log_zone_size = 0;
    sb.s_max_size = (7 + 512 + 512*512) * MINIX_BLOCK_SIZE;
    sb.s_magic = MINIX_SUPER_MAGIC;
    sb.s_state = 0x0001;
    sb.s_zones = (u16)nzones;

    u8 sbuf[MINIX_BLOCK_SIZE];
    memset(sbuf, 0, sizeof(sbuf));
    memcpy(sbuf, &sb, sizeof(sb));
    u32 lba = start_lba + (1 * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) disk_write_sector(lba + i, sbuf + (i * DISK_SECTOR_SIZE));

    u32 bitmap_total = imap_blocks + zmap_blocks;
    u32 inode_table_block = 2 + bitmap_total;
    u32 inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode);
    u32 root_inode_block = inode_table_block + (0 / inodes_per_block);
    u8 ibuf[MINIX_BLOCK_SIZE];
    memset(ibuf, 0, sizeof(ibuf));
    lba = start_lba + (root_inode_block * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) disk_read_sector(lba + i, ibuf + (i * DISK_SECTOR_SIZE));
    struct minix_inode* root = (struct minix_inode*)ibuf;
    root->i_mode = 0x41FF;
    root->i_uid = 0;
    root->i_size = 2 * sizeof(struct minix_dir_entry);
    root->i_time = 0;
    root->i_gid = 0;
    root->i_nlinks = 2;
    root->i_zone[0] = firstdatazone;
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) disk_write_sector(lba + i, ibuf + (i * DISK_SECTOR_SIZE));

    u8 dbuf[MINIX_BLOCK_SIZE];
    memset(dbuf, 0, sizeof(dbuf));
    struct minix_dir_entry* dot = (struct minix_dir_entry*)dbuf;
    dot->inode = 1;
    memcpy(dot->name, ".", 1);
    struct minix_dir_entry* dotdot = (struct minix_dir_entry*)(dbuf + 16);
    dotdot->inode = 1;
    memcpy(dotdot->name, "..", 2);
    lba = start_lba + (firstdatazone * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) disk_write_sector(lba + i, dbuf + (i * DISK_SECTOR_SIZE));

    u8 map_buf[MINIX_BLOCK_SIZE];
    memset(map_buf, 0, sizeof(map_buf));
    map_buf[0] |= 0x03;
    lba = start_lba + (2 * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) disk_write_sector(lba + i, map_buf + (i * DISK_SECTOR_SIZE));

    memset(map_buf, 0, sizeof(map_buf));
    map_buf[0] |= 0x01;
    map_buf[firstdatazone / 8] |= (u8)(1u << (firstdatazone % 8));
    lba = start_lba + ((2 + imap_blocks) * MINIX_SECTORS_PER_BLOCK);
    for (u32 i = 0; i < MINIX_SECTORS_PER_BLOCK; i++) disk_write_sector(lba + i, map_buf + (i * DISK_SECTOR_SIZE));
    return 0;
}
