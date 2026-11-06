/* ==============================================================================
 * LastKernel Enterprise Server Edition — Core Installer Compatibility Adapter
 * Bridges High-Level Modular Engines (GPT, FAT32, LastFS, RAID, Crypto) to Core
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int installer_init(void) {
    return 0;
}

int installer_probe_disks(installer_disk_t* out_disks, u32 max_disks) {
    return installer_hw_probe_disks(out_disks, max_disks);
}

int installer_partition_disk(const char* device, u64 root_size_mb, u64 swap_size_mb) {
    hw_disk_device_t disk;
    memset(&disk, 0, sizeof(disk));
    return installer_gpt_create_layout(device, root_size_mb, swap_size_mb, &disk);
}

int installer_create_raid(u32 level, const char* member_disks[], u32 disk_count) {
    return installer_raid_init_array(level, member_disks, disk_count, "md0");
}

int installer_format_partition(const char* partition_device, const char* fs_type) {
    if (!partition_device || !fs_type) return -1;
    if (strcmp(fs_type, "fat32") == 0 || strcmp(fs_type, "vfat") == 0) {
        return installer_fs_format_fat32(partition_device, "EFI-SYSTEM");
    } else if (strcmp(fs_type, "lastfs") == 0) {
        return installer_fs_format_lastfs(partition_device, "LASTOS_ROOT", 0);
    } else if (strcmp(fs_type, "swap") == 0) {
        return installer_fs_format_swap(partition_device);
    }
    return -2;
}

int installer_bootstrap_rootfs(const char* mount_point, u32 bundle_mask) {
    installer_master_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.root_mount_path, sizeof(cfg.root_mount_path), "%s", mount_point ? mount_point : "/mnt/target");
    cfg.selected_bundles = bundle_mask;
    installer_deploy_rootfs_skeleton(&cfg);
    return installer_deploy_commercial_bundles(&cfg);
}

int installer_configure_network(const installer_net_config_t* net, const char* root_mount) {
    return installer_net_generate_configs(net, net ? net->ifname : "last-server", root_mount);
}

int installer_configure_hostname(const char* hostname, const char* root_mount) {
    net_interface_profile_t net;
    memset(&net, 0, sizeof(net));
    snprintf(net.ifname, sizeof(net.ifname), "eth0");
    snprintf(net.primary_dns, sizeof(net.primary_dns), "1.1.1.1");
    snprintf(net.secondary_dns, sizeof(net.secondary_dns), "8.8.8.8");
    return installer_net_generate_configs(&net, hostname ? hostname : "last-server", root_mount);
}

int installer_configure_admin(const char* username, const char* password_hash, const char* ssh_key, const char* root_mount) {
    (void)password_hash;
    installer_master_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    installer_crypto_add_user(&cfg, username ? username : "root", "root", 1, ssh_key);
    return installer_crypto_write_userdb(&cfg, root_mount);
}

int installer_configure_user_account(const char* username, const char* password, u8 is_admin, const char* root_mount) {
    installer_master_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    installer_crypto_add_user(&cfg, username, password, is_admin, NULL);
    return installer_crypto_write_userdb(&cfg, root_mount);
}

int installer_install_bootloader(const char* boot_device, const char* root_mount, u32 kernel_profile) {
    installer_master_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.root_mount_path, sizeof(cfg.root_mount_path), "%s", root_mount ? root_mount : "/mnt/target");
    snprintf(cfg.boot_device, sizeof(cfg.boot_device), "%s", boot_device ? boot_device : "/dev/nvme0n1p1");
    cfg.kernel_profile = kernel_profile;
    return installer_deploy_bootloader(&cfg);
}

int installer_run_preflight_check(const installer_config_t* cfg) {
    return installer_verify_preflight(cfg);
}
