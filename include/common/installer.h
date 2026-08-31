/* ==============================================================================
 * LastKernel Enterprise Server Edition — Gentoo-Style CLI Installer Subsystem
 * High-Control Granular Server Provisioning Engine (Disk, RAID, Net, Bootloader)
 * ============================================================================== */

#ifndef INSTALLER_H
#define INSTALLER_H

#include "types.h"

#define INSTALLER_MAX_DISKS       16
#define INSTALLER_MAX_BUNDLES     16
#define INSTALLER_MAX_PARTITIONS  8
#define INSTALLER_PATH_MAX        128
#define INSTALLER_HOSTNAME_MAX    64

/* ── Partition Types ── */
typedef enum {
    PART_TYPE_EFI_SYSTEM   = 0xEF00,
    PART_TYPE_LASTOS_ROOT  = 0x8300,
    PART_TYPE_LASTOS_DATA  = 0x8301,
    PART_TYPE_SWAP         = 0x8200,
    PART_TYPE_RAID_MEMBER  = 0xFD00
} part_type_t;

/* ── Disk Partition Descriptor ── */
typedef struct {
    u32 partition_id;
    part_type_t part_type;
    char mount_point[32];
    char fs_type[16];          /* "lastfs", "ext4", "vfat" */
    u64 start_sector;
    u64 sector_count;
    u64 size_mb;
    u8 is_bootable;
    u8 is_formatted;
} disk_partition_t;

/* ── Physical Disk Descriptor ── */
typedef struct {
    u32 disk_id;
    char device_path[32];      /* e.g. "/dev/nvme0n1", "/dev/sda" */
    char model[40];
    u64 total_sectors;
    u64 capacity_gb;
    u32 sector_size_bytes;
    u8 is_nvme;
    u8 is_ssd;
    u32 partition_count;
    disk_partition_t partitions[INSTALLER_MAX_PARTITIONS];
} installer_disk_t;

/* ── Network Provisioning Profile ── */
typedef struct {
    char interface_name[16];   /* "eth0", "enp1s0" */
    char ip_address[32];       /* e.g. "192.168.1.100/24" */
    char gateway[32];          /* e.g. "192.168.1.1" */
    char primary_dns[32];      /* e.g. "1.1.1.1" */
    char secondary_dns[32];    /* e.g. "8.8.8.8" */
    char hostname[INSTALLER_HOSTNAME_MAX];
    u8 enable_dhcp;
    u8 enable_rss_multi_queue;
    u8 enable_bbrv3;
} installer_net_config_t;

/* ── Enterprise Default Application Bundles ── */
typedef enum {
    BUNDLE_LAST_HTTPD       = (1 << 0), /* Enterprise Rust Web Server, TLS 1.3 & WAF */
    BUNDLE_WIREGUARD_MESH   = (1 << 1), /* Kernel-accelerated VPN tunnel */
    BUNDLE_MICROVM_ENGINE   = (1 << 2), /* Intel VT-x Hypervisor & Container Jail */
    BUNDLE_PROMETHEUS_MON   = (1 << 3), /* Real-time OpenMetrics Telemetry */
    BUNDLE_LASTPKG_TOOLS    = (1 << 4), /* Userspace package management CLI */
    BUNDLE_DATABASE_SQLITE  = (1 << 5), /* Embedded SQL Engine */
    BUNDLE_DEVELOPMENT_BASE = (1 << 6), /* C compiler, toolchain & debugger */
    BUNDLE_ALL_ENTERPRISE   = 0x7F
} server_bundle_flags_t;

/* ── Master Installation Configuration ── */
typedef struct {
    char target_root_device[32];     /* e.g. "/dev/nvme0n1p2" */
    char boot_device[32];            /* e.g. "/dev/nvme0n1p1" */
    u32 raid_level;                  /* 0, 1, 5, 6 (0 = single disk) */
    u32 raid_member_count;
    char raid_members[4][32];
    
    char admin_username[32];
    char admin_password_hash[128];
    char admin_ssh_pubkey[256];
    
    u32 kernel_profile;              /* 1 = Throughput/Server, 2 = MicroVM Cloud, 3 = Realtime */
    u32 selected_bundles;            /* Bitmask of server_bundle_flags_t */
    
    installer_net_config_t net;
    
    u8 stage_bootstrap_completed;
    u8 bootloader_installed;
    u8 verification_passed;
} installer_config_t;

/* ── Installer Core API ── */
int  installer_init(void);
int  installer_probe_disks(installer_disk_t* out_disks, u32 max_disks);
int  installer_partition_disk(const char* device, u64 root_size_mb, u64 swap_size_mb);
int  installer_create_raid(u32 level, const char* member_disks[], u32 disk_count);
int  installer_format_partition(const char* partition_device, const char* fs_type);
int  installer_bootstrap_rootfs(const char* mount_point, u32 bundle_mask);
int  installer_configure_network(const installer_net_config_t* net, const char* root_mount);
int  installer_configure_admin(const char* username, const char* password_hash, const char* ssh_key, const char* root_mount);
int  installer_install_bootloader(const char* boot_device, const char* root_mount, u32 kernel_profile);
int  installer_run_preflight_check(const installer_config_t* cfg);

#endif /* INSTALLER_H */
