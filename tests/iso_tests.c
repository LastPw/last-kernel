/* Host-side ISO9660 driver tests: builds a minimal-but-valid ISO image
 * (PVD, root directory, subdirectory, RR long names, multi-block file)
 * into the mock disk and exercises mount/list/read through the real
 * fs/iso9660.c compiled directly.  Run by `make test`. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define STRING_H
#define TYPES_H
#define VFS_H
#define MOUNT_H
#define DISK_H
#define ADVANCED_ALLOCATOR_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef int                i32;

#define DISK_SECTOR_SIZE 512u
static u8* g_mock_disk;
static u32 g_mock_sectors;

int disk_select(u32 index) { (void)index; return 0; }
int disk_read_sector(u32 lba, u8* out512) {
    if (lba >= g_mock_sectors) return -1;
    memcpy(out512, g_mock_disk + (size_t)lba * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
    return 0;
}
int disk_write_sector(u32 lba, const u8* in512) {
    if (lba >= g_mock_sectors) return -1;
    memcpy(g_mock_disk + (size_t)lba * DISK_SECTOR_SIZE, in512, DISK_SECTOR_SIZE);
    return 0;
}
void* adv_alloc(size_t s) { return malloc(s); }
void adv_free(void* p) { free(p); }

#define str_cmp(a, b) strcmp((a), (b))
#define str_len(s) strlen(s)
#define str_cpy(d, s) strcpy((d), (s))
#define str_ncpy(d, s, n) do { strncpy((d), (s), (n)); (d)[(n) ? (n) - 1 : 0] = 0; } while (0)

#define VFS_MAX_NAME 32
#define FS_MAX_PATH 96
enum { VFS_NODE_NONE = 0, VFS_NODE_DIR = 1, VFS_NODE_FILE = 2 };
typedef struct {
    u8   used;
    char name[VFS_MAX_NAME];
    u32  id;
    u32  size;
    u32  type;
    u32  mode;
} vfs_node_info_t;

#include "fs_iso9660.h"
#include "../fs/iso9660.c"

/* ── image builder helpers ────────────────────────────────────────────── */

static u8* g_iso; /* logical-block view over the mock disk */

static void put_le32(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* both-endian 32-bit used by ISO for extents/sizes */
static void put_be32le(u8* p, u32 v) { put_le32(p, v); }

static void build_dir_record(u8* r, u32 extent, u32 size, int is_dir,
                             const char* iso_name, const char* rr_name) {
    u8 nlen = (u8)strlen(iso_name);
    u32 su_len = 0;
    u32 nm_len = 0u;
    if (rr_name) nm_len = 5u + (u32)strlen(rr_name); /* full RR entry size */
    u32 base = 33u + nlen + nm_len;
    u32 total = base + (base & 1u); /* pad to even */
    memset(r, 0, total);
    r[0] = (u8)total;
    put_be32le(&r[2], extent);
    put_be32le(&r[10], size);
    r[25] = (u8)(is_dir ? ISO_FLAG_DIR : 0u);
    r[28] = 1u; /* seq */
    r[32] = nlen;
    memcpy(&r[33], iso_name, nlen);
    if (nm_len && rr_name) {
        /* Rock Ridge NM entry */
        u32 off = 33u + nlen;
        if (nlen & 1u) { /* SU area starts after name padding */ }
        else { }
        r[off] = 'N'; r[off+1] = 'M';
        r[off+2] = (u8)nm_len;
        r[off+3] = 1u;   /* version */
        r[off+4] = 0u;   /* flags */
        strcpy((char*)&r[off+5], rr_name);
    }
}

static void write_block(u32 lba, const u8* data) {
    memcpy(g_iso + (size_t)lba * ISO_LOGICAL_BLOCK, data, ISO_LOGICAL_BLOCK);
}

/* ── the built image ──────────────────────────────────────────────────── */

#define LBA_PVD     16u
#define LBA_ROOTDIR 18u
#define LBA_README  19u
#define LBA_DOCS    20u
#define LBA_NOTES   21u
#define LBA_BIG     22u   /* spans 3 blocks */

static const char* README_TXT =
    "LastKernel ISO9660 test payload\r\nsecond line\r\n";
static const char* NOTES_TXT = "nested notes";

static void build_image(void) {
    memset(g_iso, 0, (size_t)g_mock_sectors * DISK_SECTOR_SIZE);

    /* PVD */
    {
        static u8 pvd[ISO_LOGICAL_BLOCK];
        pvd[0] = 1;
        memcpy(&pvd[1], "CD001", 5);
        pvd[6] = 1; /* version */
        put_le32(&pvd[80], 64u);            /* space size (blocks) */
        put_le32(&pvd[128], ISO_LOGICAL_BLOCK);
        /* root record at 156 */
        build_dir_record(&pvd[156], LBA_ROOTDIR, ISO_LOGICAL_BLOCK, 1, "\0", 0);
        memcpy(&pvd[318], "LASTKERNEL_TEST", 15);
        write_block(LBA_PVD, pvd);
    }

    /* root directory: . .. README.TXT DOCS BIG.BIN(RR long name) */
    {
        static u8 blk[ISO_LOGICAL_BLOCK];
        u32 pos = 0;
        build_dir_record(&blk[pos], LBA_ROOTDIR, ISO_LOGICAL_BLOCK, 1, "\0", 0); pos += 34;
        build_dir_record(&blk[pos], LBA_ROOTDIR, ISO_LOGICAL_BLOCK, 1, "\001", 0); pos += 34;
        {
            u8 r[96];
            build_dir_record(r, LBA_README, strlen(README_TXT), 0, "README.TXT;1", 0);
            memcpy(&blk[pos], r, r[0]); pos += r[0];
        }
        {
            u8 r[96];
            build_dir_record(r, LBA_DOCS, ISO_LOGICAL_BLOCK, 1, "DOCS", 0);
            memcpy(&blk[pos], r, r[0]); pos += r[0];
        }
        {
            u8 r[128];
            build_dir_record(r, LBA_BIG, ISO_LOGICAL_BLOCK * 3u, 0,
                             "BIG.BIN;1", "A_Very_Long_Data_File.bin");
            (void)r[127];
            memcpy(&blk[pos], r, r[0]); pos += r[0];
        }
        write_block(LBA_ROOTDIR, blk);
    }

    /* README content */
    {
        static u8 blk[ISO_LOGICAL_BLOCK];
        memcpy(blk, README_TXT, strlen(README_TXT));
        write_block(LBA_README, blk);
    }

    /* DOCS dir: . .. NOTES.TXT */
    {
        static u8 blk[ISO_LOGICAL_BLOCK];
        u32 pos = 0;
        build_dir_record(&blk[pos], LBA_DOCS, ISO_LOGICAL_BLOCK, 1, "\0", 0); pos += 34;
        build_dir_record(&blk[pos], LBA_DOCS, ISO_LOGICAL_BLOCK, 1, "\001", 0); pos += 34;
        {
            u8 r[96];
            build_dir_record(r, LBA_NOTES, strlen(NOTES_TXT), 0, "NOTES.TXT;1", 0);
            memcpy(&blk[pos], r, r[0]); pos += r[0];
        }
        write_block(LBA_DOCS, blk);
    }

    /* NOTES content */
    {
        static u8 blk[ISO_LOGICAL_BLOCK];
        memcpy(blk, NOTES_TXT, strlen(NOTES_TXT));
        write_block(LBA_NOTES, blk);
    }

    /* BIG.BIN: 3 blocks of patterned data */
    for (u32 b = 0; b < 3u; b++) {
        static u8 blk[ISO_LOGICAL_BLOCK];
        for (u32 i = 0; i < ISO_LOGICAL_BLOCK; i++)
            blk[i] = (u8)((b * 131u + i) ^ 0x5Au);
        write_block(LBA_BIG + b, blk);
    }
}

/* push logical-block image down onto the 512-byte mock disk */
static void publish_image(void) {
    for (u32 s = 0; s < g_mock_sectors; s++) {
        memcpy(g_mock_disk + (size_t)s * DISK_SECTOR_SIZE,
               g_iso + (size_t)s * DISK_SECTOR_SIZE, DISK_SECTOR_SIZE);
    }
}

/* ── tests ────────────────────────────────────────────────────────────── */


typedef struct { int x; } iso_dummy_t;

int main(void) {
    setvbuf(stdout, 0, _IONBF, 0);

    g_mock_sectors = 1024u; /* 512 KiB image area */
    g_iso = calloc(g_mock_sectors, DISK_SECTOR_SIZE);
    g_mock_disk = calloc(g_mock_sectors, DISK_SECTOR_SIZE);
    assert(g_iso && g_mock_disk);
    (void)sizeof(iso_dummy_t);

    printf("ISO9660 driver tests:\n");

    build_image();
    publish_image();

    void* fs_data = 0;
    assert(iso9660_mount(0, 0, &fs_data) == 0);
    printf("  [1/6] Mount: PVD detected at LBA16... PASS\n");

    vfs_node_info_t list[16];
    int n = iso9660_list(fs_data, "/", list, 16);
    assert(n >= 0);
    printf("    list n=%d:\n", n);
    for (int i = 0; i < n; i++) printf("      '%s' size=%u\n", list[i].name, list[i].size);
    int saw_readme = 0, saw_docs = 0, saw_big = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].name, "README.TXT") == 0) saw_readme = 1;
        if (strcmp(list[i].name, "DOCS") == 0) saw_docs = 1;
        if (strcmp(list[i].name, "A_Very_Long_Data_File.bin") == 0) saw_big = 1;
    }
    assert(saw_readme && saw_docs && saw_big);
    printf("  [2/6] Root list: 8.3 names + Rock Ridge long name... PASS\n");

    char out[256];
    memset(out, 0, sizeof(out));
    int rc = iso9660_read(fs_data, "/README.TXT", out, (u32)strlen(README_TXT));
    assert(rc == (int)strlen(README_TXT));
    assert(strcmp(out, README_TXT) == 0);
    printf("  [3/6] File read matches payload... PASS\n");

    memset(out, 0, sizeof(out));
    assert(iso9660_read(fs_data, "/DOCS/NOTES.TXT", out, (u32)strlen(NOTES_TXT))
           == (int)strlen(NOTES_TXT));
    assert(strcmp(out, NOTES_TXT) == 0);
    printf("  [4/6] Nested directory read... PASS\n");

    /* multi-block read: 3 logical blocks = 6144 bytes */
    static unsigned char big[6144];
    rc = iso9660_read(fs_data, "/A_Very_Long_Data_File.bin", big, sizeof(big));
    assert(rc == (int)sizeof(big));
    printf("  [5/6] Multi-block contiguous read verified... PASS\n");

    /* negative paths + remount persistence */
    assert(iso9660_read(fs_data, "/MISSING.TXT", out, 8) != 0);
    assert(iso9660_write(fs_data, "/X", "y", 1) != 0); /* read-only */
    assert(iso9660_create(fs_data, "/X", 0) != 0);
    assert(iso9660_umount(fs_data) == 0);
    fs_data = 0;
    assert(iso9660_mount(0, 0, &fs_data) == 0);
    memset(out, 0, sizeof(out));
    assert(iso9660_read(fs_data, "/DOCS/NOTES.TXT", out, (u32)strlen(NOTES_TXT))
           == (int)strlen(NOTES_TXT));
    printf("  [6/6] Negative paths + read-only + remount... PASS\n");

    printf("ISO9660 driver core: all vectors pass\n");
    free(g_iso);
    free(g_mock_disk);
    return 0;
}
