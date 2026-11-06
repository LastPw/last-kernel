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

static u32 fat_path_to_cluster(fat_fs_t* fs, const char* path,
                                struct msdos_dir_entry* out_de);

#define FAT_DOS_DATE_FIX 0x5821u  /* 2024-01-01 */
#define FAT_DOS_TIME_FIX 0x6000u  /* 12:00:00 */

static int fat_write_sectors(fat_fs_t* fs, u32 lba, const u8* buffer, u32 count) {
    u32 abs_lba = fs->start_lba + lba;
    for (u32 i = 0; i < count; i++) {
        if (disk_write_sector(abs_lba + i, buffer + (i * DISK_SECTOR_SIZE)) != 0) return -1;
    }
    return 0;
}

static int fat_write_cluster(fat_fs_t* fs, u32 cluster, const u8* buffer) {
    u32 sector = fs->data_start + (cluster - FAT_START_ENT) * fs->sec_per_clus;
    return fat_write_sectors(fs, sector, buffer, fs->sec_per_clus);
}

/* Raw FAT entry update with read-modify-write, mirrored to every FAT copy */
static int fat_set_next(fat_fs_t* fs, u32 cluster, u32 value) {
    if (cluster < FAT_START_ENT || !fs->fats) return -1;

    for (u8 f = 0; f < fs->fats; f++) {
        u32 fat_base = fs->fat_start + (u32)f * fs->fat_length;
        if (fs->fat_bits == 12) {
            u32 off = cluster + (cluster / 2);
            u8 buf[DISK_SECTOR_SIZE];
            if (fat_read_sectors(fs, fat_base + (off / DISK_SECTOR_SIZE), buf, 1) != 0) return -1;
            u32 idx = off % DISK_SECTOR_SIZE;
            if (cluster & 1) {
                buf[idx] = (u8)((buf[idx] & 0x0Fu) | ((value << 4) & 0xF0u));
                if (idx + 1 < DISK_SECTOR_SIZE) buf[idx + 1] = (u8)((value >> 4) & 0xFFu);
            } else {
                buf[idx] = (u8)(value & 0xFFu);
                if (idx + 1 < DISK_SECTOR_SIZE) {
                    buf[idx + 1] = (u8)((buf[idx + 1] & 0xF0u) | ((value >> 8) & 0x0Fu));
                }
            }
            if (fat_write_sectors(fs, fat_base + (off / DISK_SECTOR_SIZE), buf, 1) != 0) return -1;
        } else if (fs->fat_bits == 16) {
            u32 off = cluster * 2;
            u8 buf[DISK_SECTOR_SIZE];
            if (fat_read_sectors(fs, fat_base + (off / DISK_SECTOR_SIZE), buf, 1) != 0) return -1;
            u32 idx = off % DISK_SECTOR_SIZE;
            buf[idx] = (u8)(value & 0xFFu);
            if (idx + 1 < DISK_SECTOR_SIZE) buf[idx + 1] = (u8)((value >> 8) & 0xFFu);
            if (fat_write_sectors(fs, fat_base + (off / DISK_SECTOR_SIZE), buf, 1) != 0) return -1;
        } else {
            u32 off = cluster * 4;
            u8 buf[DISK_SECTOR_SIZE];
            if (fat_read_sectors(fs, fat_base + (off / DISK_SECTOR_SIZE), buf, 1) != 0) return -1;
            u32 idx = off % DISK_SECTOR_SIZE;
            u32 keep = (u32)buf[idx + 3] & 0xF0u; /* preserve top 4 bits */
            buf[idx] = (u8)(value & 0xFFu);
            buf[idx + 1] = (u8)((value >> 8) & 0xFFu);
            buf[idx + 2] = (u8)((value >> 16) & 0xFFu);
            if (idx + 3 < DISK_SECTOR_SIZE) buf[idx + 3] = (u8)(keep | ((value >> 24) & 0x0Fu));
            if (fat_write_sectors(fs, fat_base + (off / DISK_SECTOR_SIZE), buf, 1) != 0) return -1;
        }
    }
    return 0;
}

static u32 fat_raw_entry(fat_fs_t* fs, u32 cluster) {
    u8 buf[DISK_SECTOR_SIZE];
    u32 fat_offset;
    if (fs->fat_bits == 12) {
        fat_offset = cluster + (cluster / 2);
        if (fat_read_sectors(fs, fs->fat_start + (fat_offset / DISK_SECTOR_SIZE), buf, 1) != 0) return 0xFFFFFFFFu;
        u32 idx = fat_offset % DISK_SECTOR_SIZE;
        if (cluster & 1) {
            return ((u32)buf[idx + 1] << 4) | ((u32)buf[idx] >> 4);
        }
        return (u32)buf[idx] | (((u32)buf[idx + 1] << 8) & 0x0F00u);
    } else if (fs->fat_bits == 16) {
        fat_offset = cluster * 2;
        if (fat_read_sectors(fs, fs->fat_start + (fat_offset / DISK_SECTOR_SIZE), buf, 1) != 0) return 0xFFFFFFFFu;
        u32 idx = fat_offset % DISK_SECTOR_SIZE;
        return (u32)buf[idx] | ((u32)buf[idx + 1] << 8);
    }
    fat_offset = cluster * 4;
    if (fat_read_sectors(fs, fs->fat_start + (fat_offset / DISK_SECTOR_SIZE), buf, 1) != 0) return 0xFFFFFFFFu;
    u32 idx = fat_offset % DISK_SECTOR_SIZE;
    return ((u32)buf[idx] | ((u32)buf[idx + 1] << 8) | ((u32)buf[idx + 2] << 16) |
            ((u32)buf[idx + 3] << 24)) & FAT32_MASK;
}

static u32 fat_eoc_mark(fat_fs_t* fs) {
    if (fs->fat_bits == 12) return FAT12_EOC;
    if (fs->fat_bits == 16) return FAT16_EOC;
    return FAT32_EOC;
}

/* Allocate one free cluster and mark it end-of-chain */
static u32 fat_alloc_cluster(fat_fs_t* fs) {
    for (u32 c = FAT_START_ENT; c < fs->max_cluster; c++) {
        if (fat_raw_entry(fs, c) == 0u) {
            if (fat_set_next(fs, c, fat_eoc_mark(fs)) != 0) return 0;
            return c;
        }
    }
    return 0;
}

static int fat_free_chain(fat_fs_t* fs, u32 start) {
    u32 cur = start;
    u32 guard = fs->max_cluster + 2u;
    while (cur >= FAT_START_ENT && cur < fs->max_cluster && guard-- > 0u) {
        u32 next = fat_get_next(fs, cur);
        if (fat_set_next(fs, cur, 0u) != 0) return -1;
        if (fat_is_eoc(fs, next)) break;
        if (next < FAT_START_ENT) break;
        cur = next;
    }
    return 0;
}

/* Build an 8.3 short name. Returns -1 if the name cannot be represented
 * (no long-name support in this driver). */
static int fat_build_83(const char* in, u8 out[MSDOS_NAME]) {
    if (!in || in[0] == '\0' || in[0] == '.') return -1;
    memset(out, ' ', MSDOS_NAME);

    char base[9];
    char ext[4];
    u32 blen = 0, elen = 0;
    const char* dot = 0;
    for (const char* p = in; *p; p++) {
        if (*p == '.') { dot = p; break; }
        if (*p >= 'a' && *p <= 'z') { base[blen++] = (char)(*p - 'a' + 'A'); goto ccheck; }
        base[blen++] = *p;
ccheck:
        if (blen > 8) return -1;
    }
    if (dot) {
        dot++;
        while (*dot) {
            char c = *dot;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            if (elen >= 3) return -1;
            ext[elen++] = c;
            dot++;
        }
    }
    for (u32 i = 0; i < blen; i++) out[i] = (u8)base[i];
    for (u32 i = 0; i < elen; i++) out[8u + i] = (u8)ext[i];
    return 0;
}

/* A directory is either a cluster chain (subdirs and FAT32 root) or the
 * fixed root area (FAT12/16). */
typedef struct {
    u8  is_fixed_root;
    u32 cluster;
} fat_dirloc_t;

/* One addressable directory slot */
typedef struct {
    u32 container; /* sector offset (fixed root) or cluster number */
    u32 index;     /* entry index within container */
} fat_slotpos_t;

#define FAT_LFN_MAX_SLOTS 20u
#define FAT_LFN_NAME_MAX  256u

/* LFN checksum over the 11-byte short name (spec: rotate-right add) */
static u8 fat_lfn_checksum(const u8 name83[MSDOS_NAME]) {
    u8 sum = 0u;
    for (u32 i = 0; i < MSDOS_NAME; i++) {
        sum = (u8)(((sum >> 1) | (u8)(sum << 7)) + name83[i]);
    }
    return sum;
}

/* Assemble a UTF-16LE LFN entry payload into the char buffer.
 * Returns chars appended. Rejects non-ASCII (this driver is ASCII-only). */
static u32 fat_lfn_unpack(const struct msdos_dir_entry* de, u16* out, u32 cap) {
    static const u8 offsets[13] = { 0,2,4,6,8, 14,16,18,20,22,24, 28,30 };
    u32 n = 0;
    for (u32 i = 0; i < 13; i++) {
        u8 lo = ((const u8*)de)[offsets[i]];
        u8 hi = ((const u8*)de)[offsets[i] + 1];
        if (lo == 0x00 && hi == 0x00) break;           /* terminator */
        if (lo == 0xFF && hi == 0xFF) break;           /* padding   */
        if (hi != 0x00) return 0xFFFFFFFFu;            /* non-ASCII */
        if (n < cap) out[n++] = lo;
    }
    return n;
}

/* Any lowercase letter present? (case-preservation => LFN territory) */
static int fat_name_has_lower(const char* name) {
    for (const char* p = name; *p; p++) {
        if (*p >= 'a' && *p <= 'z') return 1;
    }
    return 0;
}

/* Does this request need a long-name slot set? */
static int fat_needs_lfn(const char* name, const u8 name83[MSDOS_NAME]) {
    u32 blen = 0, elen = 0;
    const char* dot = 0;
    for (const char* p = name; *p; p++) {
        if (*p == '.') { dot = p; break; }
        blen++;
    }
    if (dot) for (const char* p = dot + 1; *p; p++) elen++;
    if (blen + (elen ? elen + 1u : 0u) != (u32)strlen(name)) return 1; /* multiple dots etc. */

    /* lowercase anywhere -> needs LFN for case preservation */
    if (fat_name_has_lower(name)) return 1;
    /* must match what build_83 would produce */
    u8 probe[MSDOS_NAME];
    if (fat_build_83(name, probe) != 0) return 1;
    return memcmp(probe, name83, MSDOS_NAME) != 0;
}

/* Generate the short-name alias with numeric tail (~n) when needed.
 * unique_fn probes whether an alias is already taken. */
static int fat_make_alias(const char* name, u8 out[MSDOS_NAME],
                          int (*taken)(const void* ctx, const u8 alias[MSDOS_NAME]),
                          const void* ctx) {
    u8 probe[MSDOS_NAME];
    if (fat_build_83(name, probe) == 0 && !fat_name_has_lower(name)) {
        memcpy(out, probe, MSDOS_NAME);
        return 0;
    }

    char base[6];
    u32 blen = 0;
    for (const char* p = name; *p && *p != '.'; p++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (blen < 5) base[blen++] = c;
    }
    char ext[4];
    u32 elen = 0;
    const char* dot = 0;
    for (const char* p = name; *p; p++) {
        if (*p == '.') dot = p;
    }
    if (dot && dot[1]) {
        for (const char* p = dot + 1; *p && elen < 3; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            ext[elen++] = c;
        }
    }

    for (u32 n = 1; n <= 9u; n++) {
        memset(out, ' ', MSDOS_NAME);
        char tail[3] = { '~', (char)('0' + n), 0 };
        u32 tlen = 2;
        u32 pos = 0;
        u32 keep = blen > (5u - tlen) ? (5u - tlen) : blen;
        for (u32 i = 0; i < keep; i++) out[pos++] = (u8)base[i];
        for (u32 i = 0; i < tlen; i++) out[pos++] = (u8)tail[i];
        for (u32 i = 0; i < elen; i++) out[8u + i] = (u8)ext[i];
        if (!taken || !taken(ctx, out)) return 0;
    }
    return -1;
}

/* Locate an entry by its exact 8.3 image. Returns the slot index and fills
 * out_de/out_loc; *free_slot reports the first deletable/empty slot seen. */
static void fat_name_to_str(const u8* fat_name, u32 len, char* out, u32 out_sz);
static u32 fat_dir_extend(fat_fs_t* fs, const fat_dirloc_t* loc);

/* Read one directory container (fixed-root sector or cluster) */
static int fat_dir_read_container(fat_fs_t* fs, const fat_dirloc_t* loc,
                                  u32 container, u8* buf) {
    if (loc->is_fixed_root) {
        return fat_read_sectors(fs, fs->dir_start + container, buf, 1);
    }
    return fat_read_cluster(fs, container, buf);
}

static u32 fat_dir_entries_per(fat_fs_t* fs, const fat_dirloc_t* loc) {
    return loc->is_fixed_root
        ? (fs->bytes_per_sec / (u32)sizeof(struct msdos_dir_entry))
        : (fs->cluster_size / (u32)sizeof(struct msdos_dir_entry));
}

/* Case-insensitive ASCII compare */
static int fat_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Scan a directory for an entry.
 *
 * Match criteria: exact 8.3 image (name83) OR case-insensitive long-name
 * match (match_name). Either may be NULL. When a match has VFAT long-name
 * slots, their positions are returned via lfn_slots/lfn_count so callers
 * can wipe them on delete. Free-slot tracking reports the first deletable
 * or empty slot for single-entry insertion.
 *
 * Returns 1 on match, 0 when absent, -1 on I/O error. */
static int fat_dir_scan(fat_fs_t* fs, const fat_dirloc_t* loc,
                        const u8 name83[MSDOS_NAME],
                        const char* match_name,
                        struct msdos_dir_entry* out_de,
                        u32* out_index, u32* out_container,
                        u32* free_slot_index, u32* free_slot_container,
                        fat_slotpos_t* lfn_slots, u32* lfn_count,
                        char* out_longname, u32 longname_sz) {
    u8 buf[8192];
    u16 lbuf[FAT_LFN_NAME_MAX];
    u32 llen = 0;
    u8 pend_cksum = 0;
    u8 pend_expect = 0;
    u8 pend_active = 0;
    fat_slotpos_t pend_pos[FAT_LFN_MAX_SLOTS];
    u32 pend_n = 0;

    *free_slot_index = 0xFFFFFFFFu;
    *free_slot_container = 0u;
    if (lfn_count) *lfn_count = 0;

    u32 container;
    u32 guard;
    u32 fixed_containers = 0u;
    if (loc->is_fixed_root) {
        fixed_containers = ((fs->dir_entries * sizeof(struct msdos_dir_entry))
                            + fs->bytes_per_sec - 1u) / fs->bytes_per_sec;
        container = 0u;
        guard = fixed_containers + 1u;
    } else {
        container = loc->cluster;
        guard = fs->max_cluster + 4u;
    }

    while (guard-- > 0u) {
        if (!loc->is_fixed_root) {
            if (container < FAT_START_ENT || fat_is_eoc(fs, container)) break;
        }
        if (loc->is_fixed_root && container >= fixed_containers) break;
        if (fat_dir_read_container(fs, loc, container, buf) != 0) return -1;

        u32 entries = fat_dir_entries_per(fs, loc);
        struct msdos_dir_entry* de = (struct msdos_dir_entry*)buf;

        for (u32 i = 0; i < entries; i++) {
            u8 attr = de[i].attr;

            /* VFAT long-name entry */
            if ((attr & ATTR_VOLUME) == ATTR_VOLUME &&
                (attr & (ATTR_RO | ATTR_SYS | ATTR_HIDDEN)) == (ATTR_RO | ATTR_SYS | ATTR_HIDDEN) &&
                de[i].name[0] != 0xE5 && de[i].name[0] != 0x00) {
                u8 id = de[i].name[0];
                u8 seq = (u8)(id & 0x3Fu);
                if (id & 0x40u) {
                    /* first physical entry of the chain */
                    pend_active = 1u;
                    pend_n = 0u;
                    llen = 0u;
                    memset(lbuf, 0xFF, sizeof(lbuf));
                    pend_cksum = ((const u8*)&de[i])[13]; /* LFN checksum @ byte 13 */
                    pend_expect = seq;
                }
                if (!pend_active || seq != pend_expect || pend_n >= FAT_LFN_MAX_SLOTS) {
                    pend_active = 0u;
                } else {
                    pend_pos[pend_n].container = container;
                    pend_pos[pend_n].index = i;
                    pend_n++;
                    u32 got = fat_lfn_unpack(&de[i], lbuf + llen, FAT_LFN_NAME_MAX - llen);
                    if (got == 0xFFFFFFFFu) pend_active = 0u;
                    else llen += got;
                    pend_expect = (u8)(seq - 1u);
                }
                continue;
            }

            /* Regular / deleted / terminator entries end any LFN chain */
            if (de[i].name[0] == 0x00 || de[i].name[0] == 0xE5) {
                if (de[i].name[0] == 0x00 || de[i].name[0] == 0xE5) {
                    if (*free_slot_index == 0xFFFFFFFFu) {
                        *free_slot_index = i;
                        *free_slot_container = container;
                    }
                }
                if (de[i].name[0] == 0x00) {
                    if (!loc->is_fixed_root) break; /* terminator ends a chain dir */
                    /* fixed root: keep scanning later sectors? entries are
                       written sequentially; a 0x00 means nothing further */
                    goto done;
                }
                pend_active = 0u;
                continue;
            }

            if (attr == ATTR_VOLUME) continue; /* plain volume label */

            /* Short entry reached: resolve its display/match names */
            char shortname[16];
            fat_name_to_str(de[i].name, MSDOS_NAME, shortname, sizeof(shortname));

            const char* longp = 0;
            char assembled[FAT_LFN_NAME_MAX];
            if (pend_active && llen > 0u && lfn_slots) {
                if (pend_cksum == fat_lfn_checksum(de[i].name)) {
                    u32 n = llen < FAT_LFN_NAME_MAX ? llen : FAT_LFN_NAME_MAX - 1u;
                    for (u32 k = 0; k < n; k++) assembled[k] = (char)lbuf[k];
                    assembled[n] = '\0';
                    longp = assembled;
                }
            }

            int hit = 0;
            if (name83 && memcmp(de[i].name, name83, MSDOS_NAME) == 0) hit = 1;
            if (!hit && match_name) {
                if (longp && fat_ieq(longp, match_name)) hit = 1;
                else if (fat_ieq(shortname, match_name)) hit = 1;
            }

            if (hit) {
                if (out_de) *out_de = de[i];
                if (out_index) *out_index = i;
                if (out_container) *out_container = container;
                if (longp && out_longname && longname_sz > 0u) {
                    u32 n = strlen(longp);
                    if (n >= longname_sz) n = longname_sz - 1u;
                    memcpy(out_longname, longp, n);
                    out_longname[n] = '\0';
                }
                if (lfn_count && lfn_slots && pend_active) {
                    u32 n = pend_n < FAT_LFN_MAX_SLOTS ? pend_n : FAT_LFN_MAX_SLOTS;
                    for (u32 k = 0; k < n; k++) lfn_slots[k] = pend_pos[k];
                    *lfn_count = n;
                }
                return 1;
            }
            pend_active = 0u;
        }

        if (loc->is_fixed_root) {
            container++;
            if (container >= fixed_containers) break;
            continue;
        }
        container = fat_get_next(fs, container);
    }
done:
    return 0;
}

/* Collect `count` consecutive-free directory slot positions, extending a
 * chain directory once when it runs out of space. Fixed root cannot grow. */
static int fat_dir_alloc_run(fat_fs_t* fs, const fat_dirloc_t* loc, u32 count,
                             fat_slotpos_t* out) {
    u8 buf[8192];
    u32 found = 0u;
    u32 extended = 0u;
    u32 container;
    u32 fixed_containers = 0u;
    if (loc->is_fixed_root) {
        fixed_containers = ((fs->dir_entries * sizeof(struct msdos_dir_entry))
                            + fs->bytes_per_sec - 1u) / fs->bytes_per_sec;
        container = 0u;
    } else {
        container = loc->cluster;
    }

    while (found < count) {
        if (!loc->is_fixed_root) {
            if (container < FAT_START_ENT || fat_is_eoc(fs, container)) {
                if (extended) return -1;
                extended = 1u;
                u32 fresh = fat_dir_extend(fs, loc);
                if (fresh == 0) return -1;
                container = fresh; /* fresh cluster is fully zeroed -> all free */
            }
        }
        if (loc->is_fixed_root && container >= fixed_containers) return -1;
        if (fat_dir_read_container(fs, loc, container, buf) != 0) return -1;
        u32 entries = fat_dir_entries_per(fs, loc);
        struct msdos_dir_entry* de = (struct msdos_dir_entry*)buf;

        for (u32 i = 0; i < entries && found < count; i++) {
            if (de[i].name[0] == 0x00) {
                /* consume the terminator only as the LAST taken slot */
                if (found + 1u < count) {
                    if (loc->is_fixed_root) return -1; /* would erase terminator */
                }
            }
            if (de[i].name[0] == 0x00 || de[i].name[0] == 0xE5) {
                out[found].container = container;
                out[found].index = i;
                found++;
            } else {
                found = 0u; /* restart run */
            }
        }
        if (loc->is_fixed_root) {
            container++;
            if (container >= fixed_containers) break;
            continue;
        }
        if (found < count && !extended) container = fat_get_next(fs, container);
        else if (found < count && extended) break;
    }
    return found == count ? 0 : -1;
}

/* Fill one VFAT long-name slot. seq counts DOWN from N to 1; the slot
 * holding seq==N carries the 0x40 "last physical-first logical" flag. */
static void fat_fill_lfn_entry(struct msdos_dir_entry* de, u8 seq, u8 is_first,
                               u8 cksum, const u16* chars, u32 total_chars) {
    static const u8 offs[13] = { 0,2,4,6,8, 14,16,18,20,22,24, 28,30 };
    memset(((u8*)de) + 0, 0xFF, 32);
    u32 base = (u32)(seq - 1u) * 13u;
    for (u32 k = 0; k < 13; k++) {
        u32 ci = base + k;
        u16 ch = 0xFFFFu;
        if (ci < total_chars) ch = (u16)(u8)chars[ci];
        else if (ci == total_chars) ch = 0x0000u;
        ((u8*)de)[offs[k]] = (u8)(ch & 0xFFu);
        ((u8*)de)[offs[k] + 1] = (u8)(ch >> 8);
    }
    de->name[0] = (u8)(seq | (is_first ? 0x40u : 0x00u));
    de->name[1] = 0x00; /* reserved UTF16 part overwritten above anyway */
    de->attr = (u8)(ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME); /* 0x0F */
    de->nt_res = 0x00;
    de->crt_time_tenth = cksum;   /* LFN reuses this byte as checksum */
    de->start = 0x0000;
}

/* Write one directory entry back by absolute slot identity */
static int fat_dir_write_slot(fat_fs_t* fs, const fat_dirloc_t* loc,
                              u32 index, u32 container,
                              const struct msdos_dir_entry* de) {
    u8 buf[DISK_SECTOR_SIZE];
    u32 per_container;

    if (loc->is_fixed_root) {
        if (fat_read_sectors(fs, fs->dir_start + container, buf, 1) != 0) return -1;
        per_container = fs->bytes_per_sec / (u32)sizeof(struct msdos_dir_entry);
    } else {
        if (fat_read_cluster(fs, container, buf) != 0) return -1;
        per_container = fs->cluster_size / (u32)sizeof(struct msdos_dir_entry);
    }
    memcpy(buf + (index % per_container) * sizeof(struct msdos_dir_entry),
           de, sizeof(struct msdos_dir_entry));
    if (loc->is_fixed_root) {
        return fat_write_sectors(fs, fs->dir_start + container, buf, 1);
    }
    return fat_write_cluster(fs, container, buf);
}

/* Append a fresh zeroed cluster to a chain-directory */
static u32 fat_dir_extend(fat_fs_t* fs, const fat_dirloc_t* loc) {
    u32 tail = loc->cluster;
    u32 guard = fs->max_cluster + 2u;
    while (!fat_is_eoc(fs, fat_get_next(fs, tail)) && guard-- > 0u) {
        tail = fat_get_next(fs, tail);
        if (tail < FAT_START_ENT) return 0;
    }
    u32 fresh = fat_alloc_cluster(fs);
    if (fresh == 0) return 0;
    static const u8 zeros[8192];
    if (fat_write_cluster(fs, fresh, zeros) != 0) return 0;
    if (fat_set_next(fs, tail, fresh) != 0) return 0;
    return fresh;
}

static void fat_init_dirent(struct msdos_dir_entry* de, const u8 name83[MSDOS_NAME],
                            u8 attr, u32 first_cluster, u32 size) {
    memset(de, 0, sizeof(*de));
    memcpy(de->name, name83, MSDOS_NAME);
    de->attr = attr;
    de->crt_time = FAT_DOS_TIME_FIX;
    de->crt_date = FAT_DOS_DATE_FIX;
    de->time = FAT_DOS_TIME_FIX;
    de->date = FAT_DOS_DATE_FIX;
    de->lst_acc_date = FAT_DOS_DATE_FIX;
    de->start = (u16)(first_cluster & 0xFFFFu);
    de->start_hi = (u16)((first_cluster >> 16) & 0xFFFFu);
    de->size = size;
}

/* Resolve the parent directory of path into loc and return the leaf name */
static int fat_resolve_parent(fat_fs_t* fs, const char* path,
                              char* leaf, u32 leaf_sz, fat_dirloc_t* loc) {
    if (!fs || !path || path[0] != '/') return -1;
    const char* last_slash = 0;
    for (const char* p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    if (!last_slash || last_slash[1] == '\0') return -1; /* bare "/" or trailing */

    u32 li = 0;
    for (const char* p = last_slash + 1; *p && li + 1 < leaf_sz; p++) leaf[li++] = *p;
    leaf[li] = '\0';
    if (li == 0) return -1;

    if (last_slash == path) {
        loc->is_fixed_root = (u8)(fs->fat_bits != 32 ? 1 : 0);
        loc->cluster = fs->root_cluster;
        return 0;
    }

    char parent[FS_MAX_PATH];
    u32 plen = (u32)(last_slash - path);
    if (plen == 0 || plen >= FS_MAX_PATH) return -1;
    memcpy(parent, path, plen);
    parent[plen] = '\0';

    struct msdos_dir_entry pde;
    u32 pcluster = fat_path_to_cluster(fs, parent, &pde);
    if (pcluster == 0 || !(pde.attr & ATTR_DIR)) return -1;
    loc->is_fixed_root = 0;
    loc->cluster = pcluster;
    return 0;
}


/* Convert FAT-style name to normal string: lowercase, dot before ext */
static void fat_name_to_str(const u8* fat_name, u32 len, char* out, u32 out_sz) {
    u32 j = 0;
    u32 base_end = len < 8u ? len : 8u;

    for (u32 i = 0; i < base_end; i++) {
        char c = (char)fat_name[i];
        if (c == ' ' || c == 0) continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (j + 1 < out_sz) out[j++] = c;
    }
    /* extension present? */
    for (u32 i = 8u; i < len && i < MSDOS_NAME; i++) {
        if (fat_name[i] != ' ') {
            if (j + 2 < out_sz) out[j++] = '.';
            break;
        }
    }
    for (u32 i = 8u; i < len && i < MSDOS_NAME; i++) {
        char c = (char)fat_name[i];
        if (c == ' ' || c == 0) continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (j + 1 < out_sz) out[j++] = c;
    }
    out[j] = '\0';
}

/* Lookup an entry by name. Handles both directory layouts:
 * FAT12/16 fixed-area root and cluster-chain directories. */
static u32 fat_dir_find_entry(fat_fs_t* fs, u32 dir_cluster, const char* name,
                               struct msdos_dir_entry* out_de) {
    u8 name83[MSDOS_NAME];
    if (fat_build_83(name, name83) != 0) return 0;

    fat_dirloc_t loc;
    loc.is_fixed_root = (u8)((fs->fat_bits != 32 && dir_cluster == fs->root_cluster) ? 1u : 0u);
    loc.cluster = dir_cluster;

    struct msdos_dir_entry de;
    u32 si = 0, sc = 0, fi = 0, fc = 0;
    if (fat_dir_scan(fs, &loc, name83, name, &de, &si, &sc, &fi, &fc,
                     0, 0, 0, 0) != 1) return 0;
    if (out_de) *out_de = de;
    return (u32)de.start | ((u32)de.start_hi << 16);
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

        if (path[i] == '\0') {
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
/* Unified listing with VFAT long-name support */
static int fat_dir_list_ex(fat_fs_t* fs, const fat_dirloc_t* loc,
                           vfs_node_info_t* out, u32 max_out) {
    u8 buf[8192];
    u16 lbuf[FAT_LFN_NAME_MAX];
    u32 llen = 0;
    u8 pend_cksum = 0, pend_expect = 0, pend_active = 0;
    char pending_name[FAT_LFN_NAME_MAX];
    u32 count = 0;

    u32 container = loc->cluster;
    u32 guard = loc->is_fixed_root ? 1u : (fs->max_cluster + 4u);

    while (guard-- > 0u && count < max_out) {
        if (!loc->is_fixed_root) {
            if (container < FAT_START_ENT || fat_is_eoc(fs, container)) break;
        }
        if (fat_dir_read_container(fs, loc, container, buf) != 0) break;

        u32 entries = fat_dir_entries_per(fs, loc);
        struct msdos_dir_entry* de = (struct msdos_dir_entry*)buf;

        for (u32 i = 0; i < entries; i++) {
            u8 attr = de[i].attr;

            if ((attr & ATTR_VOLUME) == ATTR_VOLUME &&
                (attr & (ATTR_RO | ATTR_SYS | ATTR_HIDDEN)) == (ATTR_RO | ATTR_SYS | ATTR_HIDDEN) &&
                de[i].name[0] != 0xE5 && de[i].name[0] != 0x00) {
                u8 id = de[i].name[0];
                u8 seq = (u8)(id & 0x3Fu);
                if (id & 0x40u) {
                    pend_active = 1u;
                    llen = 0u;
                    memset(lbuf, 0xFF, sizeof(lbuf));
                    pend_cksum = ((const u8*)&de[i])[13];
                    pend_expect = seq;
                }
                if (!pend_active || seq != pend_expect) {
                    pend_active = 0u;
                } else {
                    u32 got = fat_lfn_unpack(&de[i], lbuf + llen, FAT_LFN_NAME_MAX - llen);
                    if (got == 0xFFFFFFFFu) pend_active = 0u;
                    else llen += got;
                    pend_expect = (u8)(seq - 1u);
                }
                continue;
            }

            if (de[i].name[0] == 0x00) goto done;
            if (de[i].name[0] == 0xE5) { pend_active = 0u; continue; }
            if (attr == ATTR_VOLUME) continue;

            out[count].used = 1;
            u32 startc = (u32)de[i].start | ((u32)de[i].start_hi << 16);
            out[count].id = startc;
            out[count].size = de[i].size;
            out[count].type = (de[i].attr & ATTR_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
            out[count].mode = (de[i].attr & ATTR_RO) ? 0555 : 0755;

            fat_name_to_str(de[i].name, MSDOS_NAME, out[count].name, VFS_MAX_NAME);

            if (pend_active && llen > 0u &&
                pend_cksum == fat_lfn_checksum(de[i].name)) {
                u32 n = llen < FAT_LFN_NAME_MAX ? llen : FAT_LFN_NAME_MAX - 1u;
                for (u32 k = 0; k < n; k++) pending_name[k] = (char)lbuf[k];
                pending_name[n] = '\0';
                if (strlen(pending_name) > 0u && strlen(pending_name) < VFS_MAX_NAME) {
                    memset(out[count].name, 0, VFS_MAX_NAME);
                    memcpy(out[count].name, pending_name, strlen(pending_name));
                }
            }
            pend_active = 0u;
            if (++count >= max_out) break;
        }

        if (loc->is_fixed_root) break;
        container = fat_get_next(fs, container);
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

    fs->max_cluster = FAT_START_ENT + total_clusters;

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
            fat_dirloc_t rloc;
            rloc.is_fixed_root = 0u;
            rloc.cluster = fs->root_cluster;
            return fat_dir_list_ex(fs, &rloc, out, max_out);
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
    {
        fat_dirloc_t dloc;
        dloc.is_fixed_root = 0u;
        dloc.cluster = cluster;
        return fat_dir_list_ex(fs, &dloc, out, max_out);
    }
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

/* ---------------- write support ---------------- */

int fat_write(void* fs_data, const char* path, const char* data, u32 len) {
    if (!fs_data || !path) return -1;
    fat_fs_t* fs = (fat_fs_t*)fs_data;

    char leaf[FS_MAX_PATH];
    fat_dirloc_t loc;
    if (fat_resolve_parent(fs, path, leaf, sizeof(leaf), &loc) != 0) return -1;

    u8 name83[MSDOS_NAME];
    if (fat_build_83(leaf, name83) != 0) return -1;

    struct msdos_dir_entry de;
    u32 slot_index = 0, slot_container = 0, free_index = 0, free_container = 0;
    int found = fat_dir_scan(fs, &loc, name83, leaf, &de, &slot_index, &slot_container,
                             &free_index, &free_container, 0, 0, 0, 0);
    if (found < 0) return -1;
    if (found == 0) {
        if (free_index == 0xFFFFFFFFu) return -1; /* directory full */
        fat_init_dirent(&de, name83, ATTR_ARCH, 0u, 0u);
        slot_index = free_index;
        slot_container = free_container;
    } else if (de.attr & ATTR_DIR) {
        return -1; /* cannot write through a directory entry */
    }

    /* Replace the content: free the old chain, allocate fresh space */
    u32 old_start = (u32)de.start | ((u32)de.start_hi << 16);
    if (old_start >= FAT_START_ENT) {
        (void)fat_free_chain(fs, old_start);
    }

    u32 new_start = 0u;
    if (len > 0u) {
        u32 need = (len + fs->cluster_size - 1u) / fs->cluster_size;
        u32 prev = 0u;
        for (u32 i = 0; i < need; i++) {
            u32 c = fat_alloc_cluster(fs);
            if (c == 0) {
                if (new_start) (void)fat_free_chain(fs, new_start);
                return -1;
            }
            if (prev == 0u) {
                new_start = c;
            } else {
                if (fat_set_next(fs, prev, c) != 0) {
                    (void)fat_free_chain(fs, new_start);
                    return -1;
                }
            }
            prev = c;

            u8 cbuf[8192];
            memset(cbuf, 0, sizeof(cbuf));
            u32 off = i * fs->cluster_size;
            u32 chunk = len - off;
            if (chunk > fs->cluster_size) chunk = fs->cluster_size;
            memcpy(cbuf, data + off, chunk);
            if (fat_write_cluster(fs, c, cbuf) != 0) {
                (void)fat_free_chain(fs, new_start);
                return -1;
            }
        }
        if (fat_set_next(fs, prev, fat_eoc_mark(fs)) != 0) {
            (void)fat_free_chain(fs, new_start);
            return -1;
        }
    }

    de.start = (u16)(new_start & 0xFFFFu);
    de.start_hi = (u16)((new_start >> 16) & 0xFFFFu);
    de.size = len;
    de.time = FAT_DOS_TIME_FIX;
    de.date = FAT_DOS_DATE_FIX;
    return fat_dir_write_slot(fs, &loc, slot_index, slot_container, &de);
}

/* Context for alias-uniqueness probing during create */
typedef struct {
    fat_fs_t* fs;
    const fat_dirloc_t* loc;
} fat_alias_ctx_t;

static int fat_alias_taken(const void* ctxp, const u8 alias[MSDOS_NAME]) {
    const fat_alias_ctx_t* ctx = (const fat_alias_ctx_t*)ctxp;
    struct msdos_dir_entry tmp;
    u32 si = 0, sc = 0, fi = 0, fc = 0;
    return fat_dir_scan(ctx->fs, ctx->loc, alias, 0,
                        &tmp, &si, &sc, &fi, &fc, 0, 0, 0, 0) == 1;
}

int fat_create(void* fs_data, const char* path, u8 is_dir) {
    if (!fs_data || !path) return -1;
    fat_fs_t* fs = (fat_fs_t*)fs_data;

    char leaf[FS_MAX_PATH];
    fat_dirloc_t loc;
    if (fat_resolve_parent(fs, path, leaf, sizeof(leaf), &loc) != 0) return -1;

    /* Long-name validation: ASCII printable, length caps */
    u32 nlen = (u32)str_len(leaf);
    if (nlen == 0u || nlen > FAT_LFN_NAME_MAX - 1u) return -1;
    for (u32 i = 0; i < nlen; i++) {
        unsigned char c = (unsigned char)leaf[i];
        if (c < 0x20u || c > 0x7Eu) return -1;
    }

    u8 name83[MSDOS_NAME];

    /* duplicate check by long name and by final alias */
    fat_alias_ctx_t actx = { fs, &loc };
    if (fat_make_alias(leaf, name83, fat_alias_taken, &actx) != 0) return -1;
    if (fat_alias_taken(&actx, name83)) return -1;
    struct msdos_dir_entry tmp;
    u32 si = 0, sc = 0, fi = 0, fc = 0;
    if (fat_dir_scan(fs, &loc, 0, leaf, &tmp, &si, &sc, &fi, &fc,
                     0, 0, 0, 0) == 1) return -1;

    int needs_lfn = fat_needs_lfn(leaf, name83);

    /* directory payload cluster first so a failure aborts cleanly */
    u32 first_cluster = 0u;
    u8 attr = ATTR_ARCH;
    if (is_dir) {
        first_cluster = fat_alloc_cluster(fs);
        if (first_cluster == 0) return -1;
        attr = ATTR_DIR;

        u8 dbuf[8192];
        memset(dbuf, 0, sizeof(dbuf));
        struct msdos_dir_entry* dots = (struct msdos_dir_entry*)dbuf;
        fat_init_dirent(&dots[0], (const u8*)MSDOS_DOT, ATTR_DIR, first_cluster, 0u);
        u32 parent_cluster = loc.is_fixed_root ? 0u : loc.cluster;
        fat_init_dirent(&dots[1], (const u8*)MSDOS_DOTDOT, ATTR_DIR, parent_cluster, 0u);
        if (fat_write_cluster(fs, first_cluster, dbuf) != 0) {
            (void)fat_free_chain(fs, first_cluster);
            return -1;
        }
    }

    /* slot allocation: contiguous run of N LFN entries + 1 short entry */
    u16 chars[FAT_LFN_NAME_MAX];
    u32 lfn_n = 0u;
    if (needs_lfn) {
        for (u32 k = 0; k < nlen; k++) chars[k] = (u16)(unsigned char)leaf[k];
        lfn_n = (nlen + 12u) / 13u;
    }
    u32 run = lfn_n + 1u;

    fat_slotpos_t slots[FAT_LFN_MAX_SLOTS + 1u];
    if (fat_dir_alloc_run(fs, &loc, run, slots) != 0) {
        if (first_cluster) (void)fat_free_chain(fs, first_cluster);
        return -1;
    }

    /* write VFAT entries: physical order seq=N ... seq=1 */
    u8 cksum = fat_lfn_checksum(name83);
    for (u32 k = 0; k < lfn_n; k++) {
        u8 seq = (u8)(lfn_n - k);           /* slot k holds seq N-k */
        fat_fill_lfn_entry(&tmp, seq, (u8)(k == 0u ? 1u : 0u), cksum, chars, nlen);
        if (fat_dir_write_slot(fs, &loc, slots[k].index, slots[k].container, &tmp) != 0) {
            if (first_cluster) (void)fat_free_chain(fs, first_cluster);
            return -1;
        }
    }

    fat_init_dirent(&tmp, name83, attr, first_cluster, 0u);
    if (fat_dir_write_slot(fs, &loc, slots[lfn_n].index, slots[lfn_n].container, &tmp) != 0) {
        if (first_cluster) (void)fat_free_chain(fs, first_cluster);
        return -1;
    }
    return 0;
}

int fat_delete(void* fs_data, const char* path) {
    if (!fs_data || !path || str_cmp(path, "/") == 0) return -1;
    fat_fs_t* fs = (fat_fs_t*)fs_data;

    char leaf[FS_MAX_PATH];
    fat_dirloc_t loc;
    if (fat_resolve_parent(fs, path, leaf, sizeof(leaf), &loc) != 0) return -1;

    u8 name83[MSDOS_NAME];
    if (fat_build_83(leaf, name83) != 0) return -1;

    struct msdos_dir_entry de;
    u32 si = 0, sc = 0, fi = 0, fc = 0;
    fat_slotpos_t lfn_slots[FAT_LFN_MAX_SLOTS];
    u32 lfn_count = 0u;
    if (fat_dir_scan(fs, &loc, name83, leaf, &de, &si, &sc, &fi, &fc,
                     lfn_slots, &lfn_count, 0, 0) != 1) return -1;
    if (de.attr & ATTR_VOLUME) return -1;

    u32 start = (u32)de.start | ((u32)de.start_hi << 16);

    /* Directories must be empty (only . and .. tolerated) */
    if (de.attr & ATTR_DIR) {
        if (start < FAT_START_ENT) return -1;
        u8 buf[8192];
        u32 cluster = start;
        u32 guard = fs->max_cluster + 2u;
        while (cluster >= FAT_START_ENT && !fat_is_eoc(fs, cluster) && guard-- > 0u) {
            if (fat_read_cluster(fs, cluster, buf) != 0) return -1;
            u32 entries = fs->cluster_size / (u32)sizeof(struct msdos_dir_entry);
            struct msdos_dir_entry* de_arr = (struct msdos_dir_entry*)buf;
            for (u32 i = 0; i < entries; i++) {
                if (de_arr[i].name[0] == 0x00) break;
                if (de_arr[i].name[0] == 0xE5) continue;
                if (memcmp(de_arr[i].name, MSDOS_DOT, MSDOS_NAME) == 0) continue;
                if (memcmp(de_arr[i].name, MSDOS_DOTDOT, MSDOS_NAME) == 0) continue;
                return -1; /* not empty */
            }
            cluster = fat_get_next(fs, cluster);
        }
    }

    if (start >= FAT_START_ENT) {
        if (fat_free_chain(fs, start) != 0) return -1;
    }

    de.name[0] = 0xE5;
    int rc = fat_dir_write_slot(fs, &loc, si, sc, &de);

    /* wipe the VFAT long-name slots so no ghost names remain */
    if (rc == 0 && lfn_count > 0u) {
        for (u32 k = 0; k < lfn_count && rc == 0; k++) {
            struct msdos_dir_entry ghost;
            memset(&ghost, 0xE5, sizeof(ghost));
            ghost.name[0] = 0xE5;
            rc = fat_dir_write_slot(fs, &loc, lfn_slots[k].index,
                                    lfn_slots[k].container, &ghost);
        }
    }
    return rc;
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
