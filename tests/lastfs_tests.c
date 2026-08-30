#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define STRING_H
#define TYPES_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef int                 i32;

static inline void str_ncpy(char* dst, const char* src, u32 n) {
    strncpy(dst, src, n);
    dst[n] = '\0';
}

static inline int str_cmp(const char* a, const char* b) {
    return strcmp(a, b);
}

#define DISK_SECTOR_SIZE 512u
#define MOCK_DISK_SECTORS 32768u

/* In-memory mock disk storage */
static u8 g_mock_disk[MOCK_DISK_SECTORS * DISK_SECTOR_SIZE];

/* Provide mock implementations of kernel disk primitives for unit testing */
int disk_read_sector(u32 lba, u8* buffer) {
    if (lba >= MOCK_DISK_SECTORS || !buffer) return -1;
    memcpy(buffer, g_mock_disk + (lba * DISK_SECTOR_SIZE), DISK_SECTOR_SIZE);
    return 0;
}

int disk_write_sector(u32 lba, const u8* buffer) {
    if (lba >= MOCK_DISK_SECTORS || !buffer) return -1;
    memcpy(g_mock_disk + (lba * DISK_SECTOR_SIZE), buffer, DISK_SECTOR_SIZE);
    return 0;
}

int disk_write_sector_fast(u32 lba, const u8* buffer) {
    return disk_write_sector(lba, buffer);
}

int disk_copy_sectors(u32 src_lba, u32 dst_lba, u32 count) {
    if (src_lba + count > MOCK_DISK_SECTORS || dst_lba + count > MOCK_DISK_SECTORS) return -1;
    memcpy(g_mock_disk + (dst_lba * DISK_SECTOR_SIZE),
           g_mock_disk + (src_lba * DISK_SECTOR_SIZE),
           count * DISK_SECTOR_SIZE);
    return 0;
}

void serial_writeln(const char* msg) {
    printf("  [LOG] %s\n", msg);
}

/* Include LastFS Implementation directly for host test */
#include "lastfs.h"
#include "../fs/lastfs.c"

int main(void) {
    printf("\nLastFS 2.0 (Journaling, Extents & Integrity) Tests:\n");

    memset(g_mock_disk, 0, sizeof(g_mock_disk));
    u32 start_lba = 100u;
    u32 total_lba = 16384u;

    /* Test 1: Format V2 */
    printf("  [1/6] Format LastFS 2.0 (WAL Journal & Extents)... ");
    assert(lastfs_format_v2(start_lba, total_lba) == 0);
    printf("PASS\n");

    /* Test 2: Mount */
    printf("  [2/6] Mount LastFS 2.0 Partition... ");
    assert(lastfs_mount(start_lba, total_lba) == 0);
    assert(lastfs_is_mounted() == 1);
    printf("PASS\n");

    /* Test 3: Contiguous Extent File Write and Read */
    printf("  [3/6] Extent Allocation & Contiguous Block I/O... ");
    const char* sample_data = "Hello from LastKernel 64-Bit Monolith with Modern Extent-based LastFS 2.0!";
    u32 sample_len = (u32)strlen(sample_data);

    assert(lastfs_create("system_config.json", LASTFS_TYPE_FILE) == 0);
    assert(lastfs_write("system_config.json", sample_data, sample_len) == 0);

    char read_buf[256];
    memset(read_buf, 0, sizeof(read_buf));
    int bytes_read = lastfs_read("system_config.json", read_buf, sizeof(read_buf) - 1);
    assert(bytes_read == (int)sample_len);
    assert(strcmp(read_buf, sample_data) == 0);

    lastfs_inode_t st;
    assert(lastfs_stat("system_config.json", &st) == 0);
    assert(st.size_bytes == sample_len);
    assert(st.extent_count > 0);
    printf("PASS (Extents: %u, Size: %u bytes)\n", st.extent_count, st.size_bytes);

    /* Test 4: Write-Ahead Journaling (WAL) Transaction */
    printf("  [4/6] WAL Transaction Begin & Commit... ");
    assert(lastfs_journal_begin(st.id, LASTFS_TX_WRITE, 1024u, 2u) == 0);
    assert(g_lfs.super.dirty_flag == 1u);
    assert(lastfs_journal_commit() == 0);
    assert(g_lfs.super.dirty_flag == 0u);
    printf("PASS\n");

    /* Test 5: Simulated Crash & Automatic Journal Replay */
    printf("  [5/6] Simulated Crash & Journal Replay Recovery... ");
    /* Force dirty state simulating unexpected power loss */
    g_lfs.super.dirty_flag = 1u;
    (void)flush_superblock();
    lastfs_unmount();

    /* Mount triggers replay */
    assert(lastfs_mount(start_lba, total_lba) == 0);
    assert(g_lfs.super.dirty_flag == 0u);
    printf("PASS (Filesystem Consistent)\n");

    /* Test 6: Checksum Integrity & FSCK */
    printf("  [6/6] Checksum Integrity & FSCK Check... ");
    lastfs_fsck_report_t fsck_rep;
    assert(lastfs_fsck(&fsck_rep, 1u) == 0);
    assert(fsck_rep.is_clean == 1u);
    assert(fsck_rep.checksum_errors == 0u);
    printf("PASS\n");

    lastfs_unmount();
    printf("LastFS 2.0 core: all vectors pass\n\n");
    return 0;
}
