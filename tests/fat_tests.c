/* Host-side FAT driver tests: real fs/fat.c compiled against a mock disk.
 * Exercises format, mount, create, write (chain growth + shrink), read,
 * subdirectories and delete with remount persistence. Built by `make test`. */

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

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef int                 i32;

#define DISK_SECTOR_SIZE 512u
static u8*   g_mock_disk;
static u32   g_mock_sectors;

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
void* adv_alloc(size_t size) { return malloc(size); }
void adv_free(void* p) { free(p); }

#define str_cmp(a, b) strcmp((a), (b))
#define str_ncpy(d, s, n) do { strncpy((d), (s), (n)); (d)[(n) ? (n) - 1 : 0] = 0; } while (0)

/* Minimal VFS types normally pulled from include/vfs.h */
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

#include "../include/fs_fat.h"
#include "../fs/fat.c"

#define IMG_SECTORS 16384u   /* 8 MB -> FAT16, 1 sector/cluster */

static fat_fs_t* g_fs;

static void test_format_mount(void) {
    assert(fat_format(0, 0, IMG_SECTORS) == 0);
    void* fs_data = 0;
    assert(fat_mount(0, 0, &fs_data) == 0);
    g_fs = (fat_fs_t*)fs_data;
    assert(g_fs->fat_bits == 16 || g_fs->fat_bits == 12);
    printf("  [1/9] Format + mount (%u sectors, FAT%u, clus=%u)... PASS\n",
           IMG_SECTORS, g_fs->fat_bits, g_fs->sec_per_clus);
}

static void test_create_file(void) {
    assert(fat_create(g_fs, "/HELLO.TXT", 0) == 0);
    /* duplicate create must fail */
    assert(fat_create(g_fs, "/hello.txt", 0) != 0);

    vfs_node_info_t list[16];
    int n = fat_list(g_fs, "/", list, 16);
    assert(n > 0);
    int seen = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(list[i].name, "hello.txt") == 0) {
            seen = 1;
            assert(list[i].type == VFS_NODE_FILE);
            assert(list[i].size == 0);
        }
    }
    assert(seen);
    printf("  [2/9] Create file + duplicate rejection... PASS\n");
}

static void test_write_read(void) {
    static char data[1000];
    for (u32 i = 0; i < sizeof(data); i++) data[i] = (char)(i * 7 + 3);
    assert(fat_write(g_fs, "/HELLO.TXT", data, sizeof(data)) == 0);

    static char out[2048];
    memset(out, 0, sizeof(out));
    /* this driver returns the byte count on success */
    assert(fat_read(g_fs, "/HELLO.TXT", out, sizeof(data)) == (int)sizeof(data));
    assert(memcmp(out, data, sizeof(data)) == 0);

    /* size must be reflected */
    vfs_node_info_t info;
    assert(fat_info(g_fs, "/HELLO.TXT", &info) == 0);
    assert(info.size == sizeof(data));
    printf("  [3/9] Write 1000B (multi-cluster chain) + read back... PASS\n");
}

static void test_overwrite_grow(void) {
    static char big[3000];
    for (u32 i = 0; i < sizeof(big); i++) big[i] = (char)(i ^ 0x5A);
    assert(fat_write(g_fs, "/HELLO.TXT", big, sizeof(big)) == 0);

    static char out[4096];
    memset(out, 0xEE, sizeof(out));
    assert(fat_read(g_fs, "/HELLO.TXT", out, sizeof(big)) == (int)sizeof(big));
    assert(memcmp(out, big, sizeof(big)) == 0);

    vfs_node_info_t info;
    assert(fat_info(g_fs, "/HELLO.TXT", &info) == 0);
    assert(info.size == sizeof(big));
    printf("  [4/9] Overwrite grow to 3000B (fresh chain alloc)... PASS\n");
}

static void test_overwrite_shrink(void) {
    static char small[100];
    memcpy(small, "truncate me", 11);
    assert(fat_write(g_fs, "/HELLO.TXT", small, sizeof(small)) == 0);

    static char out[512];
    memset(out, 0x77, sizeof(out));
    assert(fat_read(g_fs, "/HELLO.TXT", out, sizeof(small)) == (int)sizeof(small));
    assert(memcmp(out, small, sizeof(small)) == 0);
    /* stale tail from previous content must not leak past new size */
    assert(out[sizeof(small)] == 0x77);

    vfs_node_info_t info;
    assert(fat_info(g_fs, "/HELLO.TXT", &info) == 0);
    assert(info.size == sizeof(small));
    printf("  [5/9] Overwrite shrink to 100B (no stale tail)... PASS\n");
}

static void test_subdirectories(void) {
    assert(fat_create(g_fs, "/DATA", 1) == 0);
    assert(fat_create(g_fs, "/DATA/A.TXT", 0) == 0);

    const char* msg = "nested file payload";
    assert(fat_write(g_fs, "/DATA/A.TXT", msg, strlen(msg)) == 0);

    char out[64];
    memset(out, 0, sizeof(out));
    assert(fat_read(g_fs, "/DATA/A.TXT", out, (u32)strlen(msg)) == (int)strlen(msg));
    assert(strcmp(out, msg) == 0);

    vfs_node_info_t list[16];
    int n = fat_list(g_fs, "/DATA", list, 16);
    assert(n >= 3); /* . .. A.TXT */
    printf("  [6/9] Subdirectory + nested file write/read... PASS\n");
}

static void test_delete(void) {
    /* non-empty dir must be rejected */
    assert(fat_delete(g_fs, "/DATA") != 0);

    assert(fat_delete(g_fs, "/DATA/A.TXT") == 0);
    assert(fat_read(g_fs, "/DATA/A.TXT", (char*)g_mock_disk, 4) != 0);
    assert(fat_delete(g_fs, "/HELLO.TXT") == 0);
    assert(fat_delete(g_fs, "/HELLO.TXT") != 0); /* double delete */

    vfs_node_info_t list[16];
    int n = fat_list(g_fs, "/", list, 16);
    for (int i = 0; i < n; i++) {
        assert(strcmp(list[i].name, "hello.txt") != 0);
    }
    printf("  [7/9] Delete files, empty-dir guard on rmdir... PASS\n");
}

static void test_persistence(void) {
    const char* msg = "survives remount";
    assert(fat_write(g_fs, "/KEEP.BIN", msg, strlen(msg)) == 0);

    assert(fat_umount(g_fs) == 0);
    void* fs_data = 0;
    assert(fat_mount(0, 0, &fs_data) == 0);
    g_fs = (fat_fs_t*)fs_data;

    char out[64];
    memset(out, 0, sizeof(out));
    assert(fat_read(g_fs, "/KEEP.BIN", out, (u32)strlen(msg)) == (int)strlen(msg));
    assert(strcmp(out, msg) == 0);
    printf("  [8/9] Write-through persistence across remount... PASS\n");
}

static void test_exhaustion_guard(void) {
    /* Fill every cluster; allocation failure must be reported, not corrupt */
    char chunk[400];
    memset(chunk, 'X', sizeof(chunk));
    u32 written = 0;
    char path[32];
    for (u32 i = 0; i < 30000u; i++) {
        snprintf(path, sizeof(path), "/F%05u.BIN", (unsigned)i % 100000u);
        if (fat_write(g_fs, path, chunk, sizeof(chunk)) != 0) break;
        written++;
    }
    assert(written < 30000u);          /* must eventually refuse */
    assert(written > 100u);            /* ...but only after real capacity */
    printf("  [9/9] Disk-full reported cleanly after %u files... PASS\n",
           (unsigned)written);
}

int main(void) {
    g_mock_disk = calloc(IMG_SECTORS, DISK_SECTOR_SIZE);
    g_mock_sectors = IMG_SECTORS;
    assert(g_mock_disk);

    printf("FAT driver tests:\n");
    test_format_mount();
    test_create_file();
    test_write_read();
    test_overwrite_grow();
    test_overwrite_shrink();
    test_subdirectories();
    test_delete();
    test_persistence();
    test_exhaustion_guard();
    printf("FAT driver core: all vectors pass\n");

    free(g_mock_disk);
    return 0;
}
