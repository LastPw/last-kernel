/* ==============================================================================
 * LastKernel Enterprise Server Edition — Commercial Installer Subsystem
 * Complete Architecture Definitions: GPT, FAT32 BPB, LastFS 2.0, RAID GF(2^8),
 * Hardware Diagnostics, PBKDF2-SHA512 UserDB, Network Bonding & TUI Engine.
 * ============================================================================== */

#ifndef INSTALLER_H
#define INSTALLER_H

#include "types.h"
#include <stddef.h>

#define INSTALLER_VERSION_STR       "2.0.0-Enterprise"
#define INSTALLER_MAX_DISKS         32
#define INSTALLER_MAX_PARTITIONS    128
#define INSTALLER_MAX_RAID_MEMBERS  16
#define INSTALLER_MAX_INTERFACES    16
#define INSTALLER_MAX_USERS         64
#define INSTALLER_MAX_BUNDLES       16
#define INSTALLER_PATH_MAX          512
#define INSTALLER_NAME_MAX          64
#define INSTALLER_UUID_STR_MAX      37

/* ══════════════════════════════════════════════════════════════════════════════
 * 1. GPT (GUID Partition Table) & Protective MBR Binary Specifications
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct __attribute__((packed)) {
    u8  boot_indicator;        /* 0x00 = Non-bootable, 0x80 = Bootable */
    u8  start_chs[3];
    u8  os_type;               /* 0xEE for GPT Protective MBR */
    u8  end_chs[3];
    u32 starting_lba;          /* 1 */
    u32 size_in_lba;
} mbr_part_entry_t;

typedef struct __attribute__((packed)) {
    u8  boot_code[446];
    mbr_part_entry_t entries[4];
    u16 signature;             /* 0xAA55 */
} legacy_mbr_sector_t;

typedef struct __attribute__((packed)) {
    u8  signature[8];          /* "EFI PART" (0x5452415020494645ULL) */
    u32 revision;              /* 0x00010000 (v1.0) */
    u32 header_size;           /* 92 bytes */
    u32 header_crc32;
    u32 reserved;
    u64 my_lba;                /* 1 */
    u64 alternate_lba;         /* Backup GPT LBA (Total LBA - 1) */
    u64 first_usable_lba;      /* Typically 34 */
    u64 last_usable_lba;       /* Total LBA - 34 */
    u8  disk_guid[16];         /* RFC 4122 UUID */
    u64 partition_entry_lba;   /* LBA 2 */
    u32 num_partition_entries; /* 128 */
    u32 sizeof_partition_entry;/* 128 bytes */
    u32 partition_entry_array_crc32;
} gpt_header_sector_t;

typedef struct __attribute__((packed)) {
    u8  type_guid[16];         /* Partition Type GUID */
    u8  unique_guid[16];       /* Unique Partition GUID */
    u64 starting_lba;
    u64 ending_lba;
    u64 attributes;            /* Bit 0: System, Bit 2: Legacy BIOS Boot, Bit 60: Read-Only */
    u16 partition_name[36];    /* UTF-16LE Partition Name */
} gpt_partition_entry_t;

/* Standard GUIDs in raw byte format */
extern const u8 GUID_EFI_SYSTEM[16];      /* C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
extern const u8 GUID_LASTOS_ROOT[16];     /* 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
extern const u8 GUID_LASTOS_DATA[16];     /* EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 */
extern const u8 GUID_LINUX_SWAP[16];      /* 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F */
extern const u8 GUID_LINUX_RAID[16];      /* A19D880F-05FC-4D3B-A006-743F0F84911E */

/* ══════════════════════════════════════════════════════════════════════════════
 * 2. FAT32 (EFI System Partition) Binary Data Structures
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct __attribute__((packed)) {
    u8  jump_boot[3];          /* 0xEB, 0x58, 0x90 */
    char oem_name[8];          /* "MSWIN4.1" or "LASTOS32" */
    u16 bytes_per_sector;      /* 512 */
    u8  sectors_per_cluster;   /* 8 (4KB cluster) */
    u16 reserved_sector_count; /* 32 */
    u8  num_fats;              /* 2 */
    u16 root_entry_count;      /* 0 for FAT32 */
    u16 total_sectors_16;      /* 0 for FAT32 */
    u8  media_type;            /* 0xF8 (Fixed disk) */
    u16 fat_size_16;           /* 0 for FAT32 */
    u16 sectors_per_track;     /* 63 */
    u16 num_heads;             /* 255 */
    u32 hidden_sectors;        /* LBA offset */
    u32 total_sectors_32;      /* Partition sector count */
    
    /* FAT32 Extended BPB */
    u32 fat_size_32;           /* Sectors per FAT */
    u16 ext_flags;             /* Active FAT mirroring */
    u16 fs_version;            /* 0x0000 */
    u32 root_cluster;          /* 2 */
    u16 fs_info;               /* Sector 1 */
    u16 bk_boot_sec;           /* Sector 6 (Backup Boot Sector) */
    u8  reserved[12];
    u8  drive_number;          /* 0x80 */
    u8  reserved1;
    u8  boot_signature;        /* 0x29 */
    u32 volume_id;             /* Serial Number */
    char volume_label[11];     /* "EFI SYSTEM " */
    char fs_type_str[8];       /* "FAT32   " */
} fat32_boot_sector_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * 3. LastFS 2.0 Superblock & Disk Layout Specifications
 * ══════════════════════════════════════════════════════════════════════════════ */

#define LASTFS_MAGIC        0x4C415354  /* "LAST" */
#define LASTFS_VERSION      2
#define LASTFS_BLOCK_SIZE   4096
#define LASTFS_INODE_SIZE   256

typedef struct __attribute__((packed)) {
    u32 magic;                 /* 0x4C415354 */
    u32 version;               /* 2 */
    u32 block_size;            /* 4096 */
    u64 total_blocks;
    u64 free_blocks;
    u32 total_inodes;
    u32 free_inodes;
    u64 inode_table_lba;
    u64 block_bitmap_lba;
    u64 inode_bitmap_lba;
    u64 journal_start_lba;
    u32 journal_blocks;
    u64 root_dir_inode;        /* Inode 1 */
    u8  volume_uuid[16];
    char volume_label[32];
    u64 mount_time;
    u64 write_time;
    u32 mount_count;
    u32 max_mount_count;
    u32 state_flags;           /* 0 = Clean, 1 = Errors */
    u32 crc32;
} lastfs_superblock_t;

typedef struct __attribute__((packed)) {
    u16 mode;                  /* File mode (type + permissions) */
    u16 uid;
    u32 size_bytes;
    u32 atime;
    u32 ctime;
    u32 mtime;
    u32 dtime;
    u16 gid;
    u16 links_count;
    u32 blocks_count;
    u32 flags;
    u64 direct_blocks[12];
    u64 indirect_block;
    u64 double_indirect_block;
    u8  reserved[64];
} lastfs_raw_inode_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * 4. Hardware Diagnostics & Disk Storage Types
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    DISK_BUS_UNKNOWN = 0,
    DISK_BUS_NVME,
    DISK_BUS_SATA_AHCI,
    DISK_BUS_SAS,
    DISK_BUS_VIRTIO,
    DISK_BUS_SCSI
} disk_bus_type_t;

typedef struct {
    u32 partition_id;
    u8  type_guid[16];
    char mount_point[64];
    char fs_type[16];          /* "lastfs", "fat32", "ext4", "swap" */
    char label[32];
    u64 start_lba;
    u64 sector_count;
    u64 size_mb;
    u8  is_bootable;
    u8  is_formatted;
} disk_partition_info_t;

typedef struct {
    u32 disk_id;
    char device_path[64];      /* e.g. "/dev/nvme0n1", "/dev/sda" */
    char model[64];
    char serial[32];
    char firmware_rev[16];
    disk_bus_type_t bus_type;
    u64 total_sectors;
    u64 capacity_gb;
    u32 sector_size_bytes;     /* 512 or 4096 */
    u8  is_nvme;
    u8  is_ssd;
    u8  smart_healthy;
    u32 temperature_celsius;
    u32 percentage_used;       /* NVMe wear level (0-100%) */
    u32 partition_count;
    disk_partition_info_t partitions[INSTALLER_MAX_PARTITIONS];
} hw_disk_device_t;

typedef struct {
    u32 cpu_count;
    u32 numa_nodes_count;
    char cpu_model[64];
    u64 total_ram_mb;
    u64 free_ram_mb;
    u8  has_vmx_hypervisor;    /* Intel VT-x or AMD-V */
    u8  has_pkeys;             /* Memory Protection Keys */
    u8  has_aes_ni;
    u8  has_avx512;
} hw_system_summary_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * 5. Network Interface & Bonding Configuration
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    NET_MODE_DHCP = 0,
    NET_MODE_STATIC,
    NET_MODE_BONDING,
    NET_MODE_VLAN
} net_config_mode_t;

typedef enum {
    BOND_MODE_BALANCE_RR = 0,
    BOND_MODE_ACTIVE_BACKUP = 1,
    BOND_MODE_LACP_8023AD = 4
} net_bond_mode_t;

typedef struct {
    char ifname[16];           /* "eth0", "enp1s0" */
    char mac_address[18];      /* "52:54:00:12:34:56" */
    net_config_mode_t mode;
    char ip_address[64];       /* "192.168.1.100/24" or "2001:db8::1/64" */
    char gateway[64];          /* "192.168.1.1" */
    char primary_dns[64];      /* "1.1.1.1" */
    char secondary_dns[64];    /* "8.8.8.8" */
    u16  vlan_id;              /* 0 = Untagged, 1-4094 */
    net_bond_mode_t bond_mode;
    u8   enable_rss_multi_queue;
    u8   enable_bbrv3;
} net_interface_profile_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * 6. User Account Database & Security Credentials
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char username[32];
    u32  uid;
    u32  gid;
    u8   is_admin;             /* 1 = Superuser / Sudoer */
    char salt_hex[33];         /* 16-byte random salt in hex */
    char password_hash[129];   /* 64-byte SHA-512 / PBKDF2 in hex */
    char ssh_pubkey[512];      /* e.g. "ssh-ed25519 AAAAC3NzaC1lZDI1..." */
    char shell[32];            /* "/bin/sh" */
    char home_dir[64];         /* "/root" or "/home/username" */
} installer_user_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * 7. Enterprise Bundles & Master Installation Context
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef enum {
    BUNDLE_LAST_HTTPD       = (1 << 0), /* Enterprise Rust Web Server, TLS 1.3 & WAF */
    BUNDLE_WIREGUARD_MESH   = (1 << 1), /* Kernel-accelerated VPN tunnel */
    BUNDLE_MICROVM_ENGINE   = (1 << 2), /* Intel VT-x Hypervisor & Container Jail */
    BUNDLE_PROMETHEUS_MON   = (1 << 3), /* Real-time OpenMetrics Telemetry */
    BUNDLE_LASTPKG_TOOLS    = (1 << 4), /* Userspace package management CLI */
    BUNDLE_DATABASE_SQLITE  = (1 << 5), /* Embedded SQL Engine */
    BUNDLE_DEVELOPMENT_BASE = (1 << 6), /* C toolchain, headers & debugger */
    BUNDLE_ALL_ENTERPRISE   = 0x7F
} server_bundle_flags_t;

typedef struct {
    char target_disk[64];            /* "/dev/nvme0n1" */
    char boot_device[64];            /* "/dev/nvme0n1p1" */
    char root_device[64];            /* "/dev/nvme0n1p2" */
    char swap_device[64];            /* "/dev/nvme0n1p3" */
    char root_mount_path[256];       /* "/mnt/target" or custom mount */
    
    u32  raid_level;                 /* 0, 1, 5, 6 (0 = Single Disk) */
    u32  raid_member_count;
    char raid_members[INSTALLER_MAX_RAID_MEMBERS][64];
    
    char hostname[INSTALLER_NAME_MAX];
    char timezone[64];               /* "UTC", "Asia/Tehran", etc. */
    char locale[32];                 /* "en_US.UTF-8" */
    
    u32  user_count;
    installer_user_t users[INSTALLER_MAX_USERS];
    
    u32  kernel_profile;             /* 1 = Server Throughput, 2 = MicroVM Cloud, 3 = Realtime */
    u32  selected_bundles;           /* Bitmask of server_bundle_flags_t */
    
    net_interface_profile_t net;
    
    u8   is_unattended;
    u8   enable_full_disk_encryption;
    char luks_passphrase[128];
} installer_master_config_t;

/* ══════════════════════════════════════════════════════════════════════════════
 * 8. Subsystem API Declarations
 * ══════════════════════════════════════════════════════════════════════════════ */

/* CRC32 & UUID Utilities */
u32  installer_crc32(const void* data, size_t length);
void installer_generate_uuid_v4(u8 out_uuid[16]);
void installer_uuid_to_string(const u8 uuid[16], char out_str[INSTALLER_UUID_STR_MAX]);

/* Hardware Diagnostics */
int  installer_hw_probe_system(hw_system_summary_t* out_sys);
int  installer_hw_probe_disks(hw_disk_device_t* out_disks, u32 max_disks);

/* GPT & Partitioning Engine */
int  installer_gpt_create_layout(const char* device_path, u64 root_size_mb, u64 swap_size_mb, hw_disk_device_t* disk_info);
int  installer_gpt_write_table(int disk_fd, const hw_disk_device_t* disk_info);

/* Filesystem Formatting */
int  installer_fs_format_fat32(const char* device_path, const char* volume_label);
int  installer_fs_format_lastfs(const char* device_path, const char* volume_label, u64 partition_size_bytes);
int  installer_fs_format_swap(const char* device_path);

/* RAID Engine with Galois Field GF(2^8) */
int  installer_raid_init_array(u32 level, const char* member_disks[], u32 disk_count, const char* array_name);
void installer_raid_gf256_calc_parity(const u8* const* data_blocks, u32 data_count, u8* p_block, u8* q_block, size_t block_size);

/* Cryptographic UserDB & Password Engine */
void installer_crypto_pbkdf2_sha512(const char* password, const char* salt_hex, u32 iterations, u8 out_hash[64]);
int  installer_crypto_add_user(installer_master_config_t* cfg, const char* username, const char* password, u8 is_admin, const char* ssh_pubkey);
int  installer_crypto_write_userdb(const installer_master_config_t* cfg, const char* root_mount);

/* Network & OS Skeleton Deployer */
int  installer_net_generate_configs(const net_interface_profile_t* net, const char* hostname, const char* root_mount);
int  installer_deploy_rootfs_skeleton(const installer_master_config_t* cfg);
int  installer_deploy_commercial_bundles(const installer_master_config_t* cfg);
int  installer_deploy_bootloader(const installer_master_config_t* cfg);
int  installer_verify_preflight(const installer_master_config_t* cfg);

/* Terminal User Interface (TUI) Engine */
void installer_tui_init(void);
void installer_tui_render_header(const char* current_step_title, int current_step, int total_steps);
int  installer_tui_menu_select(const char* title, const char* options[], int option_count);
void installer_tui_input_dialog(const char* prompt, const char* default_val, char* out_buf, size_t buf_size, u8 is_password);
void installer_tui_progress_bar(const char* task_name, int current_pct, int total_pct);
int  installer_tui_run_wizard(installer_master_config_t* cfg);

/* Legacy Compatibility Wrappers (Matching include/common/installer.h) */
typedef hw_disk_device_t installer_disk_t;
typedef net_interface_profile_t installer_net_config_t;
typedef installer_master_config_t installer_config_t;

int  installer_init(void);
int  installer_probe_disks(installer_disk_t* out_disks, u32 max_disks);
int  installer_partition_disk(const char* device, u64 root_size_mb, u64 swap_size_mb);
int  installer_create_raid(u32 level, const char* member_disks[], u32 disk_count);
int  installer_format_partition(const char* partition_device, const char* fs_type);
int  installer_bootstrap_rootfs(const char* mount_point, u32 bundle_mask);
int  installer_configure_network(const installer_net_config_t* net, const char* root_mount);
int  installer_configure_hostname(const char* hostname, const char* root_mount);
int  installer_configure_admin(const char* username, const char* password_hash, const char* ssh_key, const char* root_mount);
int  installer_configure_user_account(const char* username, const char* password, u8 is_admin, const char* root_mount);
int  installer_install_bootloader(const char* boot_device, const char* root_mount, u32 kernel_profile);
int  installer_run_preflight_check(const installer_config_t* cfg);

#endif /* INSTALLER_H */
