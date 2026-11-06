/* ==============================================================================
 * LastKernel Enterprise Server Edition — GPT & Partitioning Engine
 * Full Binary Serializer for GUID Partition Tables & Protective MBR
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

/* Standard GUID Definitions (Big-endian / Mixed-endian RFC 4122 byte arrays) */
const u8 GUID_EFI_SYSTEM[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

const u8 GUID_LASTOS_ROOT[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

const u8 GUID_LASTOS_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

const u8 GUID_LINUX_SWAP[16] = {
    0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43, 0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F
};

const u8 GUID_LINUX_RAID[16] = {
    0x0F, 0x88, 0x9D, 0xA1, 0xFC, 0x05, 0x3B, 0x4D, 0xA0, 0x06, 0x74, 0x3F, 0x0F, 0x84, 0x91, 0x1E
};

/* ── Standard CRC32 Implementation ── */
u32 installer_crc32(const void* data, size_t length) {
    const u8* bytes = (const u8*)data;
    u32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ── UUID Version 4 Generator (RFC 4122) ── */
void installer_generate_uuid_v4(u8 out_uuid[16]) {
    static u32 s_seed = 0;
    if (s_seed == 0) {
        s_seed = (u32)time(NULL) ^ (u32)getpid();
        srand(s_seed);
    }
    for (int i = 0; i < 16; i++) {
        out_uuid[i] = (u8)(rand() & 0xFF);
    }
    /* Version 4: set bits 4-7 of byte 7 to 0100 */
    out_uuid[6] = (out_uuid[6] & 0x0F) | 0x40;
    /* Variant 1: set bits 6-7 of byte 8 to 10 */
    out_uuid[8] = (out_uuid[8] & 0x3F) | 0x80;
}

void installer_uuid_to_string(const u8 uuid[16], char out_str[INSTALLER_UUID_STR_MAX]) {
    snprintf(out_str, INSTALLER_UUID_STR_MAX,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5],
        uuid[6], uuid[7],
        uuid[8], uuid[9],
        uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

/* ── GPT Partition Layout Calculator with 1MB Alignment ── */
int installer_gpt_create_layout(const char* device_path, u64 root_size_mb, u64 swap_size_mb, hw_disk_device_t* disk_info) {
    if (!device_path || !disk_info) return -1;

    strncpy(disk_info->device_path, device_path, sizeof(disk_info->device_path) - 1);
    u32 sector_sz = disk_info->sector_size_bytes ? disk_info->sector_size_bytes : 4096;
    u64 total_sec = disk_info->total_sectors;

    if (total_sec == 0) {
        total_sec = (disk_info->capacity_gb * 1024ULL * 1024ULL * 1024ULL) / sector_sz;
        disk_info->total_sectors = total_sec;
    }

    /* 1MB Alignment = (1024 * 1024) / sector_sz */
    u64 align_sec = (1024ULL * 1024ULL) / sector_sz;
    u64 current_lba = align_sec * 2; /* Start at 2MB LBA */
    disk_info->partition_count = 0;

    /* 1. Partition 1: EFI System Partition (512 MB) */
    disk_partition_info_t* p1 = &disk_info->partitions[disk_info->partition_count++];
    p1->partition_id = 1;
    memcpy(p1->type_guid, GUID_EFI_SYSTEM, 16);
    snprintf(p1->mount_point, sizeof(p1->mount_point), "/boot/efi");
    snprintf(p1->fs_type, sizeof(p1->fs_type), "fat32");
    snprintf(p1->label, sizeof(p1->label), "EFI-SYSTEM");
    p1->size_mb = 512;
    p1->start_lba = current_lba;
    p1->sector_count = (p1->size_mb * 1024ULL * 1024ULL) / sector_sz;
    p1->is_bootable = 1;
    p1->is_formatted = 0;
    current_lba += p1->sector_count;

    /* Align next LBA */
    if (current_lba % align_sec) current_lba += (align_sec - (current_lba % align_sec));

    /* 2. Partition 2: LastOS Root System Partition */
    disk_partition_info_t* p2 = &disk_info->partitions[disk_info->partition_count++];
    p2->partition_id = 2;
    memcpy(p2->type_guid, GUID_LASTOS_ROOT, 16);
    snprintf(p2->mount_point, sizeof(p2->mount_point), "/");
    snprintf(p2->fs_type, sizeof(p2->fs_type), "lastfs");
    snprintf(p2->label, sizeof(p2->label), "LASTOS-ROOT");
    
    u64 remaining_mb = (total_sec * sector_sz) / (1024ULL * 1024ULL) - 512 - swap_size_mb - 32;
    p2->size_mb = root_size_mb > 0 ? root_size_mb : remaining_mb;
    p2->start_lba = current_lba;
    p2->sector_count = (p2->size_mb * 1024ULL * 1024ULL) / sector_sz;
    p2->is_bootable = 0;
    p2->is_formatted = 0;
    current_lba += p2->sector_count;

    /* Align next LBA */
    if (current_lba % align_sec) current_lba += (align_sec - (current_lba % align_sec));

    /* 3. Partition 3: Linux Swap (if requested) */
    if (swap_size_mb > 0) {
        disk_partition_info_t* p3 = &disk_info->partitions[disk_info->partition_count++];
        p3->partition_id = 3;
        memcpy(p3->type_guid, GUID_LINUX_SWAP, 16);
        snprintf(p3->mount_point, sizeof(p3->mount_point), "[SWAP]");
        snprintf(p3->fs_type, sizeof(p3->fs_type), "swap");
        snprintf(p3->label, sizeof(p3->label), "LASTOS-SWAP");
        p3->size_mb = swap_size_mb;
        p3->start_lba = current_lba;
        p3->sector_count = (p3->size_mb * 1024ULL * 1024ULL) / sector_sz;
        p3->is_bootable = 0;
        p3->is_formatted = 0;
    }

    return 0;
}

/* ── Serialize and Write Real Binary GPT Header and Partition Array ── */
int installer_gpt_write_table(int disk_fd, const hw_disk_device_t* disk_info) {
    if (disk_fd < 0 || !disk_info) return -1;

    u32 sector_sz = disk_info->sector_size_bytes ? disk_info->sector_size_bytes : 512;
    u64 total_lba = disk_info->total_sectors ? disk_info->total_sectors : (disk_info->capacity_gb * 1024ULL * 1024ULL * 1024ULL / sector_sz);

    /* 1. Construct Protective MBR (LBA 0) */
    legacy_mbr_sector_t mbr;
    memset(&mbr, 0, sizeof(mbr));
    mbr.entries[0].boot_indicator = 0x00;
    mbr.entries[0].start_chs[0] = 0x00;
    mbr.entries[0].start_chs[1] = 0x02;
    mbr.entries[0].start_chs[2] = 0x00;
    mbr.entries[0].os_type = 0xEE; /* GPT Protective */
    mbr.entries[0].end_chs[0] = 0xFF;
    mbr.entries[0].end_chs[1] = 0xFF;
    mbr.entries[0].end_chs[2] = 0xFF;
    mbr.entries[0].starting_lba = 1;
    mbr.entries[0].size_in_lba = total_lba > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (u32)(total_lba - 1);
    mbr.signature = 0xAA55;

    (void)pwrite(disk_fd, &mbr, sizeof(mbr), 0);

    /* 2. Construct 128 GPT Partition Entries (LBA 2 to 33) */
    gpt_partition_entry_t entries[128];
    memset(entries, 0, sizeof(entries));

    for (u32 i = 0; i < disk_info->partition_count && i < 128; i++) {
        const disk_partition_info_t* p = &disk_info->partitions[i];
        memcpy(entries[i].type_guid, p->type_guid, 16);
        installer_generate_uuid_v4(entries[i].unique_guid);
        entries[i].starting_lba = p->start_lba;
        entries[i].ending_lba = p->start_lba + p->sector_count - 1;
        entries[i].attributes = p->is_bootable ? (1ULL << 2) : 0ULL;

        /* Convert ASCII label to UTF-16LE */
        for (size_t k = 0; k < 36 && p->label[k]; k++) {
            entries[i].partition_name[k] = (u16)p->label[k];
        }
    }

    u32 entries_crc32 = installer_crc32(entries, sizeof(entries));

    /* 3. Construct Primary GPT Header (LBA 1) */
    gpt_header_sector_t gpt_hdr;
    memset(&gpt_hdr, 0, sizeof(gpt_hdr));
    memcpy(gpt_hdr.signature, "EFI PART", 8);
    gpt_hdr.revision = 0x00010000;
    gpt_hdr.header_size = 92;
    gpt_hdr.my_lba = 1;
    gpt_hdr.alternate_lba = total_lba - 1;
    gpt_hdr.first_usable_lba = 34;
    gpt_hdr.last_usable_lba = total_lba - 34;
    installer_generate_uuid_v4(gpt_hdr.disk_guid);
    gpt_hdr.partition_entry_lba = 2;
    gpt_hdr.num_partition_entries = 128;
    gpt_hdr.sizeof_partition_entry = sizeof(gpt_partition_entry_t);
    gpt_hdr.partition_entry_array_crc32 = entries_crc32;
    gpt_hdr.header_crc32 = installer_crc32(&gpt_hdr, 92);

    /* Write Primary GPT Header & Partition Array */
    (void)pwrite(disk_fd, &gpt_hdr, sizeof(gpt_hdr), 1 * sector_sz);
    (void)pwrite(disk_fd, entries, sizeof(entries), 2 * sector_sz);

    /* 4. Construct and Write Backup GPT Header at End of Disk */
    gpt_header_sector_t backup_hdr = gpt_hdr;
    backup_hdr.my_lba = total_lba - 1;
    backup_hdr.alternate_lba = 1;
    backup_hdr.partition_entry_lba = total_lba - 33;
    backup_hdr.header_crc32 = 0;
    backup_hdr.header_crc32 = installer_crc32(&backup_hdr, 92);

    (void)pwrite(disk_fd, entries, sizeof(entries), (total_lba - 33) * sector_sz);
    (void)pwrite(disk_fd, &backup_hdr, sizeof(backup_hdr), (total_lba - 1) * sector_sz);

    return 0;
}
