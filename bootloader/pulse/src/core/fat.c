#include "fat.h"
#include "lib.h"

/* ---- on-disk constants ---- */
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LONG_NAME (0x0F)
#define ATTR_MASK      0x3F

#define FAT12_EOC      0x0FF8u
#define FAT16_EOC      0xFFF8u
#define FAT32_EOC      0x0FFFFFF8u

typedef struct {
    const blkdev_t *dev;
    UINT64 base;                /* partition start LBA */

    UINTN   bytes_per_sector;
    UINTN   sec_per_clus;
    UINTN   reserved_sectors;
    UINTN   nfats;
    UINTN   fatsz_sectors;      /* per FAT */
    UINTN   root_entries;       /* FAT12/16 */
    UINT32  total_sectors;
    UINT32  fat_start;          /* in volume sectors */
    UINT32  root_start;         /* FAT12/16 dir region */
    UINT32  data_start;         /* cluster 2 base */
    UINT32  root_cluster;       /* FAT32 */
    INT32   type;               /* 12/16/32 */
} fat_mount_t;

/* mount arena: BPB mirror + bounce buffer, sized by vfs rules */
#define ARENA_NEED (512)

typedef struct {
    UINT64 size;
    UINT32 start_cluster;
} fat_node_priv;

static fat_mount_t g_mnt;
static UINT8 g_bounce[4096];
static INT32 g_mounted;

static INT32 rd_sec(UINT64 lba, VOID *buf)
{
    return g_mnt.dev->read_sector(g_mnt.dev->ctx,
                                  g_mnt.base + lba, buf);
}

/* ---------------- probe / mount ---------------- */

static UINT16 rd16(const UINT8 *p) { return (UINT16)(p[0] | p[1] << 8); }
static UINT32 rd32(const UINT8 *p)
{
    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static INT32 fat_probe(const blkdev_t *dev)
{
    UINT8 sb[512];
    if (!dev || dev->read_sector(dev->ctx, 0, sb) != FS_OK)
        return FS_E_PROBE;
    if (rd16(sb + 510) != 0xAA55)
        return FS_E_PROBE;
    UINTN bps = rd16(sb + 11);
    UINTN spc = sb[13];
    if (bps < 512 || bps > 4096 || (bps & (bps - 1)))
        return FS_E_PROBE;
    if (spc == 0 || (spc & (spc - 1)) || spc > 128)
        return FS_E_PROBE;
    if (rd16(sb + 14) == 0 || sb[16] == 0 || sb[16] > 2)
        return FS_E_PROBE;
    return FS_OK;
}

static INT32 fat_mount(const blkdev_t *dev, VOID *arena, UINTN cap)
{
    (VOID)arena;
    (VOID)cap;
    if (fat_probe(dev) != FS_OK)
        return FS_E_PROBE;

    UINT8 sb[512];
    memset(&g_mnt, 0, sizeof(g_mnt));
    if (dev->read_sector(dev->ctx, 0, sb) != FS_OK)
        return FS_E_IO;
    g_mnt.dev = dev;
    g_mnt.bytes_per_sector = rd16(sb + 11);
    g_mnt.sec_per_clus = sb[13];
    g_mnt.reserved_sectors = rd16(sb + 14);
    g_mnt.nfats = sb[16];
    g_mnt.root_entries = rd16(sb + 17);
    UINT32 tot16 = rd16(sb + 19);
    UINT32 fatsz16 = rd16(sb + 22);
    UINT32 tot32 = rd32(sb + 32);
    UINT32 fatsz32 = rd32(sb + 36);
    g_mnt.total_sectors = tot16 ? tot16 : tot32;
    g_mnt.root_cluster = rd32(sb + 44);

    g_mnt.fatsz_sectors = fatsz16 ? fatsz16 : fatsz32;
    g_mnt.fat_start = g_mnt.reserved_sectors;

    UINT32 root_dir_sectors =
        rd16(sb + 17)
            ? (rd16(sb + 17) * 32 + g_mnt.bytes_per_sector - 1) /
                  g_mnt.bytes_per_sector
            : 0;                        /* FAT32: no fixed root region */
    UINT32 first_data = g_mnt.fat_start +
                        g_mnt.nfats * g_mnt.fatsz_sectors + root_dir_sectors;
    g_mnt.data_start = first_data;

    UINT32 clusters =
        (g_mnt.total_sectors - first_data) / g_mnt.sec_per_clus;

    if (clusters < 4085)
        g_mnt.type = 12;
    else if (clusters < 65525)
        g_mnt.type = 16;
    else {
        g_mnt.type = 32;
        if (g_mnt.root_cluster < 2) {
            g_mounted = 0;
            return FS_E_CORRUPT;
        }
    }

    if (g_mnt.type == 12 || g_mnt.type == 16)
        g_mnt.root_start = g_mnt.reserved_sectors +
                           g_mnt.nfats * g_mnt.fatsz_sectors;
    else
        g_mnt.root_start = 0;   /* unused: chain from root_cluster */

    g_mounted = 1;
    return FS_OK;
}

void fat_unmount(void)
{
    g_mounted = 0;
}

/* ---------------- FAT chain ---------------- */

static INT32 fat_entry(UINT32 clus, UINT32 *out)
{
    UINTN fsec, off;
    switch (g_mnt.type) {
    case 12:
        fsec = (UINTN)clus * 3 / 2 / g_mnt.bytes_per_sector;
        off = (UINTN)clus * 3 / 2 % g_mnt.bytes_per_sector;
        break;
    case 16:
        fsec = (UINTN)clus * 2 / g_mnt.bytes_per_sector;
        off = (UINTN)clus * 2 % g_mnt.bytes_per_sector;
        break;
    default:
        fsec = (UINTN)clus * 4 / g_mnt.bytes_per_sector;
        off = (UINTN)clus * 4 % g_mnt.bytes_per_sector;
        break;
    }
    if (rd_sec(g_mnt.fat_start + fsec, g_bounce) != FS_OK)
        return FS_E_IO;
    if (g_mnt.type == 12 && off == g_mnt.bytes_per_sector - 1) {
        static UINT8 bounce2[4096];
        if (rd_sec(g_mnt.fat_start + fsec + 1, bounce2) != FS_OK)
            return FS_E_IO;
        UINT16 v = (UINT16)(g_bounce[off] | (UINT16)bounce2[0] << 8);
        *out = (clus & 1) ? v >> 4 : v & 0xFFF;
        return FS_OK;
    }
    switch (g_mnt.type) {
    case 12: {
        UINT16 v = rd16(g_bounce + off);
        *out = (clus & 1) ? v >> 4 : v & 0xFFF;
        break;
    }
    case 16:
        *out = rd16(g_bounce + off);
        break;
    default:
        *out = rd32(g_bounce + off) & 0x0FFFFFFFu;
        break;
    }
    return FS_OK;
}

static INT32 eoc(UINT32 v)
{
    switch (g_mnt.type) {
    case 12:  return v >= FAT12_EOC;
    case 16:  return v >= FAT16_EOC;
    default:  return v >= FAT32_EOC;
    }
}

/* read one full cluster (spc sectors) into dst, which must hold
   sec_per_clus*bytes_per_sector bytes */
static INT32 read_cluster(UINT32 clus, VOID *dst)
{
    UINT32 s = g_mnt.data_start +
               (clus - 2) * (UINT32)g_mnt.sec_per_clus;
    for (UINTN i = 0; i < g_mnt.sec_per_clus; i++)
        if (rd_sec(s + i, (UINT8 *)dst + i * g_mnt.bytes_per_sector) != FS_OK)
            return FS_E_IO;
    return FS_OK;
}

/* ---------------- name matching ---------------- */

static int ascii_eq_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
        char y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void short_name(const UINT8 *de, char out[13])
{
    UINTN n = 0;
    for (UINTN i = 0; i < 8 && de[i] != ' '; i++)
        out[n++] = (char)de[i];
    if (de[8] != ' ') {
        out[n++] = '.';
        for (UINTN i = 8; i < 11 && de[i] != ' '; i++)
            out[n++] = (char)de[i];
    }
    out[n] = 0;
}

/* assemble LFN name from the run of entries preceding `de`;
   run_start points at the last LFN entry (seq has 0x40). */
static INT32 lfn_name(const UINT8 *run, UINTN count, char *out, UINTN cap)
{
    UINTN n = 0;
    /* fragments appear in reverse order: run[0] is highest seq */
    for (UINTN i = 0; i < count; i++) {
        const UINT8 *e = run + i * 32;
        static const UINTN pos[] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        for (UINTN k = 0; k < 13; k++) {
            UINT16 ch = rd16(e + pos[k]);
            if (ch == 0xFFFF || ch == 0)
                goto done;
            if (n + 1 < cap)
                out[n++] = (char)ch;
        }
    }
done:
    out[n] = 0;
    return FS_OK;
}

/* ---------------- lookup ---------------- */

/* LFN-aware variant: scans with a sliding window of long-name entries */
static INT32 dir_find(const char *comp, UINT32 dir_cluster,
                      UINT32 fixed_sectors, UINT32 *clus, UINT32 *size,
                      UINT8 *attr)
{
    UINTN bps = g_mnt.bytes_per_sector;

    UINT8 sector[4096];
    UINT32 s_idx = 0, c = dir_cluster;
    UINTN guard = 0;

    for (;;) {
        UINTN limit = bps;
        if (g_mnt.type == 32 || !fixed_sectors) {
            if (read_cluster(c, sector) != FS_OK)
                return FS_E_IO;
            limit = (UINTN)g_mnt.sec_per_clus * bps;
        } else {
            if (s_idx >= fixed_sectors)
                break;
            if (rd_sec(g_mnt.root_start + s_idx, sector) != FS_OK)
                return FS_E_IO;
        }

        for (UINTN o = 0; o < limit; o += 32) {
            const UINT8 *de = sector + o;
            UINT8 a = de[11];
            if (de[0] == 0)
                goto not_found;
            if (de[0] == 0xE5 || a == ATTR_LONG_NAME ||
                (a & ATTR_VOLUME_ID))
                continue;

            /* plain entry: check LFN window above it */
            char name[256];
            UINTN cnt = 0;
            while (cnt < o / 32 &&
                   sector[o - (cnt + 1) * 32 + 11] == ATTR_LONG_NAME)
                cnt++;
            if (cnt) {
                lfn_name(sector + o - cnt * 32, cnt, name, sizeof(name));
                if (ascii_eq_nocase(name, comp))
                    goto hit;
            }
            short_name(de, name);
            if (ascii_eq_nocase(name, comp))
                goto hit;

            continue;
hit:
            *clus = (UINT32)rd16(de + 20) << 16 | rd16(de + 26);
            *size = rd32(de + 28);
            *attr = a;
            return FS_OK;
        }

        if (g_mnt.type == 32 || !fixed_sectors) {
            if (++guard > 1000000u)
                return FS_E_CORRUPT;
            if (fat_entry(c, &c) != FS_OK)
                return FS_E_IO;
            if (eoc(c))
                break;
        } else {
            s_idx++;
        }
    }
not_found:
    return FS_E_NOTFOUND;
}

static INT32 split_first(const char *path, char *comp, const char **rest)
{
    while (*path == '/')
        path++;
    if (!*path)
        return FS_E_NOTFOUND;
    UINTN n = 0;
    while (path[n] && path[n] != '/') {
        if (n >= 255)
            return FS_E_RANGE;
        comp[n] = path[n];
        n++;
    }
    comp[n] = 0;
    *rest = path + n;
    return FS_OK;
}

/* ---------------- vfs surface ---------------- */

static INT32 fat_lookup(const char *path, vfs_node_t *out)
{
    if (!g_mounted || !path || !out)
        return FS_E_RANGE;

    UINT32 cur_cluster = g_mnt.type == 32
                             ? g_mnt.root_cluster
                             : 2;                 /* ignored for fixed */
    UINT32 fixed = g_mnt.type == 32 ? 0
                                    : (g_mnt.root_entries * 32 +
                                       g_mnt.bytes_per_sector - 1) /
                                          g_mnt.bytes_per_sector;
    UINT8 is_dir = 1;
    INT32 first_level = 1;

    char comp[256];
    const char *rest = path;
    while (*rest) {
        INT32 rc = split_first(rest, comp, &rest);
        if (rc != FS_OK)
            return rc;
        if (!is_dir)
            return FS_E_NOTFOUND;
        UINT32 clus, size;
        UINT8 attr;
        rc = dir_find(comp, cur_cluster,
                      first_level ? fixed : 0, &clus, &size, &attr);
        first_level = 0;
        if (rc != FS_OK)
            return rc;
        is_dir = (attr & ATTR_DIRECTORY) != 0;
        cur_cluster = clus;
        out->size = size;
        out->flags = (is_dir ? VFS_F_DIR : VFS_F_FILE) | VFS_F_RO;
        fat_node_priv p;
        p.size = size;
        p.start_cluster = clus;
        memcpy(out->priv, &p, sizeof(p));
    }
    return FS_OK;
}

static INT32 fat_read(const vfs_node_t *node, UINT64 off,
                      VOID *buf, UINTN len, UINTN *got)
{
    if (!g_mounted || !node || !buf)
        return FS_E_RANGE;
    fat_node_priv p;
    memcpy(&p, node->priv, sizeof(p));
    if (off > p.size)
        off = p.size;
    UINT64 avail = p.size - off;
    UINTN want = len < avail ? len : (UINTN)avail;
    *got = 0;

    UINTN bps = g_mnt.bytes_per_sector;
    UINT32 clus = p.start_cluster;
    UINT64 skip = off;

    /* advance to starting cluster */
    while (skip >= (UINT64)bps * g_mnt.sec_per_clus) {
        skip -= (UINT64)bps * g_mnt.sec_per_clus;
        if (fat_entry(clus, &clus) != FS_OK)
            return FS_E_IO;
    }

    UINTN done = 0;
    UINTN guard = 0;
    static UINT8 clusbuf[4096];
    while (done < want) {
        if (++guard > 268435456u)
            return FS_E_CORRUPT;
        if (clus < 2 || read_cluster(clus, clusbuf) != FS_OK)
            return FS_E_IO;
        UINTN cap = (UINTN)bps * g_mnt.sec_per_clus;
        UINTN take = cap - (UINTN)skip > want - done
                         ? want - done
                         : cap - (UINTN)skip;
        memcpy((UINT8 *)buf + done, clusbuf + skip, take);
        done += take;
        skip = 0;
        if (done >= want)
            break;
        if (fat_entry(clus, &clus) != FS_OK)
            return FS_E_IO;
        if (eoc(clus))
            break;                       /* short file: stop at EOC */
    }
    *got = done;
    return FS_OK;
}

const fs_driver_t fat_driver = {
    .name   = "fat",
    .probe  = fat_probe,
    .mount  = fat_mount,
    .lookup = fat_lookup,
    .read   = fat_read,
};
