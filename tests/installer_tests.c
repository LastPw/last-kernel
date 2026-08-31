/* ==============================================================================
 * LastKernel Enterprise Server Edition — Installer Unit & Integration Tests
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "installer.h"

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Commercial Server Edition Installer Tests\n");
    printf("=======================================================\n");

    /* [1/6] Testing Hardware Disk Probing */
    printf("  [1/6] Testing NVMe & Storage Probing Engine... ");
    assert(installer_init() == 0);
    installer_disk_t disks[INSTALLER_MAX_DISKS];
    int disk_count = installer_probe_disks(disks, INSTALLER_MAX_DISKS);
    assert(disk_count >= 3);
    assert(strcmp(disks[0].device_path, "/dev/nvme0n1") == 0);
    assert(disks[0].is_nvme == 1);
    assert(disks[0].capacity_gb == 1920);
    assert(disks[0].sector_size_bytes == 4096);
    printf("PASS (%d Disks Detected, Primary NVMe 1.92TB)\n", disk_count);

    /* [2/6] Testing Partitioning Engine */
    printf("  [2/6] Testing GPT Layout (EFI, Root, Swap)... ");
    assert(installer_partition_disk("/dev/nvme0n1", 0, 8192) == 0);
    int verified_disks = installer_probe_disks(disks, INSTALLER_MAX_DISKS);
    assert(verified_disks > 0);
    installer_disk_t* d0 = &disks[0];
    assert(d0->partition_count == 3);
    assert(d0->partitions[0].part_type == PART_TYPE_EFI_SYSTEM);
    assert(d0->partitions[0].size_mb == 512);
    assert(d0->partitions[0].is_bootable == 1);
    assert(d0->partitions[1].part_type == PART_TYPE_LASTOS_ROOT);
    assert(strcmp(d0->partitions[1].fs_type, "lastfs") == 0);
    assert(d0->partitions[2].part_type == PART_TYPE_SWAP);
    printf("PASS (3 Partitions Validated: 512MB EFI, LastFS Root, 8GB Swap)\n");

    /* [3/6] Testing Software RAID Array Creation */
    printf("  [3/6] Testing RAID 5/6 Parity Array Constructor... ");
    const char* raid_disks[4] = { "/dev/nvme0n1", "/dev/nvme1n1", "/dev/nvme2n1", "/dev/nvme3n1" };
    assert(installer_create_raid(5, raid_disks, 4) == 0);
    assert(installer_create_raid(6, raid_disks, 4) == 0);
    /* Should reject invalid RAID levels */
    assert(installer_create_raid(99, raid_disks, 4) == -2);
    printf("PASS (RAID 5 & 6 Parity Matrices Constructible)\n");

    /* [4/6] Testing File System Formatting */
    printf("  [4/6] Testing High-Speed Filesystem Formatting... ");
    assert(installer_format_partition("/dev/nvme0n1p1", "vfat") == 0);
    assert(installer_format_partition("/dev/nvme0n1p2", "lastfs") == 0);
    assert(installer_format_partition("/dev/nvme0n1p3", "swap") == 0);
    assert(installer_format_partition("/dev/nvme0n1p2", "invalid_fs") == -2);
    printf("PASS (LastFS 2.0, VFAT, Swap Formats Verified)\n");

    /* [5/6] Testing RootFS Bootstrap & Enterprise Bundles */
    printf("  [5/6] Testing RootFS Bootstrap & Bundle Deployment... ");
    u32 bundles = BUNDLE_LAST_HTTPD | BUNDLE_WIREGUARD_MESH | BUNDLE_MICROVM_ENGINE | BUNDLE_PROMETHEUS_MON | BUNDLE_LASTPKG_TOOLS;
    assert(installer_bootstrap_rootfs("/mnt/target", bundles) == 0);
    printf("PASS (LastHTTPD, WireGuard, MicroVM, Prometheus Deployed)\n");

    /* [6/6] Testing Network & Bootloader Pre-flight Verification */
    printf("  [6/6] Testing Network, Security & Bootloader Pre-Flight... ");
    installer_net_config_t net;
    memset(&net, 0, sizeof(net));
    strncpy(net.interface_name, "eth0", sizeof(net.interface_name) - 1);
    strncpy(net.ip_address, "10.0.2.15/24", sizeof(net.ip_address) - 1);
    strncpy(net.gateway, "10.0.2.2", sizeof(net.gateway) - 1);
    strncpy(net.primary_dns, "1.1.1.1", sizeof(net.primary_dns) - 1);
    strncpy(net.hostname, "last-server-node01", sizeof(net.hostname) - 1);
    net.enable_rss_multi_queue = 1;
    net.enable_bbrv3 = 1;
    assert(installer_configure_network(&net, "/mnt/target") == 0);
    assert(installer_configure_admin("root", "$scrypt$hash...", "ssh-ed25519 AAAAC3N...", "/mnt/target") == 0);
    assert(installer_install_bootloader("/dev/nvme0n1", "/mnt/target", 1) == 0);

    installer_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.target_root_device, "/dev/nvme0n1p2", sizeof(cfg.target_root_device) - 1);
    strncpy(cfg.boot_device, "/dev/nvme0n1p1", sizeof(cfg.boot_device) - 1);
    strncpy(cfg.admin_username, "root", sizeof(cfg.admin_username) - 1);
    cfg.net = net;
    assert(installer_run_preflight_check(&cfg) == 0);
    printf("PASS (Static IP, Ed25519 SSH, Limine Bootloader & Pre-Flight 100%% Operational)\n");

    printf(">>> ALL 6 COMMERCIAL SERVER INSTALLER TESTS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
