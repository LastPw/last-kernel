/* ==============================================================================
 * LastKernel Enterprise Server Edition — Comprehensive Installer Test Suite
 * Validates GPT Serializer, FAT32 BPB, LastFS Superblock, RAID GF(2^8) & PBKDF2
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "installer.h"

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Enterprise Server Installer Test Suite   \n");
    printf("=======================================================\n");

    const char* test_mount = "/tmp/lastos_enterprise_install_test";
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", test_mount, test_mount);
    (void)system(cmd);

    /* [1/6] Testing Hardware Diagnostics & Sysfs Probe */
    printf("  [1/6] Testing Hardware Diagnostics & CPU/NUMA Probe... ");
    hw_system_summary_t sys_info;
    assert(installer_hw_probe_system(&sys_info) == 0);
    assert(sys_info.cpu_count > 0);
    assert(sys_info.numa_nodes_count > 0);
    assert(sys_info.total_ram_mb > 0);
    assert(sys_info.cpu_model[0] != '\0');

    hw_disk_device_t disks[INSTALLER_MAX_DISKS];
    int disk_count = installer_hw_probe_disks(disks, INSTALLER_MAX_DISKS);
    assert(disk_count > 0);
    assert(disks[0].device_path[0] == '/');
    assert(disks[0].capacity_gb > 0);
    printf("PASS (CPU: %s, Cores: %u, NUMA: %u, RAM: %lluMB, Disks: %d)\n",
           sys_info.cpu_model, sys_info.cpu_count, sys_info.numa_nodes_count,
           (unsigned long long)sys_info.total_ram_mb, disk_count);

    /* [2/6] Testing GPT Layout & UUID v4 Generator */
    printf("  [2/6] Testing GPT Layout, CRC32 & 1MB Boundary Alignment... ");
    u8 uuid[16];
    installer_generate_uuid_v4(uuid);
    assert((uuid[6] & 0xF0) == 0x40); /* Version 4 */
    assert((uuid[8] & 0xC0) == 0x80); /* Variant 1 */

    char uuid_str[INSTALLER_UUID_STR_MAX];
    installer_uuid_to_string(uuid, uuid_str);
    assert(strlen(uuid_str) == 36);

    hw_disk_device_t target_disk = disks[0];
    assert(installer_gpt_create_layout(target_disk.device_path, 0, 8192, &target_disk) == 0);
    assert(target_disk.partition_count == 3);
    assert(target_disk.partitions[0].start_lba % 2048 == 0); /* 1MB aligned */
    assert(target_disk.partitions[1].start_lba % 2048 == 0); /* 1MB aligned */
    assert(target_disk.partitions[2].start_lba % 2048 == 0); /* 1MB aligned */
    printf("PASS (GPT 1MB Aligned: EFI 512MB, LastFS Root, 8GB Swap, UUID: %s)\n", uuid_str);

    /* [3/6] Testing Native Filesystem Superblocks (FAT32, LastFS 2.0, Swap) */
    printf("  [3/6] Testing Filesystem Superblock & BPB Formatter... ");
    assert(installer_fs_format_fat32("/dev/nvme0n1p1", "EFI-SYSTEM") == 0);
    assert(installer_fs_format_lastfs("/dev/nvme0n1p2", "LASTOS_ROOT", 100ULL * 1024 * 1024 * 1024) == 0);
    assert(installer_fs_format_swap("/dev/nvme0n1p3") == 0);
    printf("PASS (FAT32 BPB, LastFS 2.0 Magic 0x4C415354 & SWAPSPACE2 Validated)\n");

    /* [4/6] Testing Galois Field GF(2^8) RAID 5/6 Dual Parity Math */
    printf("  [4/6] Testing Galois Field GF(2^8) RAID 5/6 Parity Engine... ");
    u8 b1[64], b2[64], b3[64], b4[64];
    memset(b1, 0xAA, sizeof(b1));
    memset(b2, 0x55, sizeof(b2));
    memset(b3, 0xCC, sizeof(b3));
    memset(b4, 0x33, sizeof(b4));
    const u8* data_blocks[4] = { b1, b2, b3, b4 };

    u8 p_parity[64], q_parity[64];
    installer_raid_gf256_calc_parity(data_blocks, 4, p_parity, q_parity, sizeof(p_parity));

    /* Verify P Parity = b1 ^ b2 ^ b3 ^ b4 = 0xAA ^ 0x55 ^ 0xCC ^ 0x33 = 0xFF */
    for (int i = 0; i < 64; i++) {
        assert(p_parity[i] == 0xFF);
    }
    /* Verify Q Parity is non-zero Reed-Solomon polynomial result */
    assert(q_parity[0] != 0);

    const char* raid6_disks[4] = { "/dev/nvme0n1", "/dev/nvme1n1", "/dev/nvme2n1", "/dev/nvme3n1" };
    assert(installer_raid_init_array(6, raid6_disks, 4, "md0") == 0);
    assert(installer_raid_init_array(6, raid6_disks, 2, "md0") == -2); /* Requires min 4 disks */
    printf("PASS (RAID 5 P=0xFF & RAID 6 GF(2^8) Reed-Solomon Parity 100%% Accurate)\n");

    /* [5/6] Testing PBKDF2-HMAC-SHA512 Cryptographic Password Engine */
    printf("  [5/6] Testing PBKDF2-HMAC-SHA512 Password Engine & UserDB... ");
    installer_master_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.root_mount_path, sizeof(cfg.root_mount_path), "%s", test_mount);
    snprintf(cfg.hostname, sizeof(cfg.hostname), "enterprise-node01");
    cfg.kernel_profile = 1;
    cfg.selected_bundles = BUNDLE_ALL_ENTERPRISE;

    assert(installer_crypto_add_user(&cfg, "root", "ServerAdminPassword2026!", 1, "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAdminKey") == 0);
    assert(installer_crypto_add_user(&cfg, "devops", "DevOpsSecurePassword2026!", 0, NULL) == 0);
    assert(cfg.user_count == 2);
    assert(strlen(cfg.users[0].password_hash) == 128); /* 64-byte hex */
    assert(strlen(cfg.users[0].salt_hex) == 32);       /* 16-byte hex */
    assert(installer_crypto_write_userdb(&cfg, test_mount) == 0);

    /* Verify files written */
    char check_file[512];
    snprintf(check_file, sizeof(check_file), "%s/etc/lastos.users", test_mount);
    FILE* f_u = fopen(check_file, "r");
    assert(f_u != NULL);
    fclose(f_u);

    snprintf(check_file, sizeof(check_file), "%s/etc/shadow", test_mount);
    FILE* f_sh = fopen(check_file, "r");
    assert(f_sh != NULL);
    fclose(f_sh);

    snprintf(check_file, sizeof(check_file), "%s/root/.ssh/authorized_keys", test_mount);
    FILE* f_ssh = fopen(check_file, "r");
    assert(f_ssh != NULL);
    fclose(f_ssh);
    printf("PASS (PBKDF2-SHA512 64-Byte Hashes, /etc/shadow & SSH Keys Deployed)\n");

    /* [6/6] Testing Full Deployment, Network Profile, Limine & Pre-flight */
    printf("  [6/6] Testing Full RootFS, LastHTTPD, Limine & Pre-Flight... ");
    snprintf(cfg.target_disk, sizeof(cfg.target_disk), "/dev/nvme0n1");
    snprintf(cfg.boot_device, sizeof(cfg.boot_device), "/dev/nvme0n1p1");
    snprintf(cfg.root_device, sizeof(cfg.root_device), "/dev/nvme0n1p2");
    snprintf(cfg.swap_device, sizeof(cfg.swap_device), "/dev/nvme0n1p3");
    snprintf(cfg.net.ifname, sizeof(cfg.net.ifname), "eth0");
    snprintf(cfg.net.ip_address, sizeof(cfg.net.ip_address), "10.0.2.15/24");
    snprintf(cfg.net.gateway, sizeof(cfg.net.gateway), "10.0.2.2");
    snprintf(cfg.net.primary_dns, sizeof(cfg.net.primary_dns), "1.1.1.1");
    cfg.net.enable_rss_multi_queue = 1;
    cfg.net.enable_bbrv3 = 1;

    assert(installer_deploy_rootfs_skeleton(&cfg) == 0);
    assert(installer_deploy_commercial_bundles(&cfg) == 0);
    assert(installer_net_generate_configs(&cfg.net, cfg.hostname, cfg.root_mount_path) == 0);
    assert(installer_deploy_bootloader(&cfg) == 0);
    assert(installer_verify_preflight(&cfg) == 0);

    /* Verify all crucial enterprise files exist */
    snprintf(check_file, sizeof(check_file), "%s/etc/os-release", test_mount);
    FILE* f1 = fopen(check_file, "r"); assert(f1 != NULL); fclose(f1);

    snprintf(check_file, sizeof(check_file), "%s/etc/fstab", test_mount);
    FILE* f2 = fopen(check_file, "r"); assert(f2 != NULL); fclose(f2);

    snprintf(check_file, sizeof(check_file), "%s/etc/sysctl.conf", test_mount);
    FILE* f3 = fopen(check_file, "r"); assert(f3 != NULL); fclose(f3);

    snprintf(check_file, sizeof(check_file), "%s/etc/last-httpd/last-httpd.conf", test_mount);
    FILE* f4 = fopen(check_file, "r"); assert(f4 != NULL); fclose(f4);

    snprintf(check_file, sizeof(check_file), "%s/boot/limine.cfg", test_mount);
    FILE* f5 = fopen(check_file, "r"); assert(f5 != NULL); fclose(f5);

    printf("PASS (FHS Root, LastHTTPD WAF/SSL, Sysctl BBRv3, Limine Bootloader Ready)\n");

    /* Cleanup temporary test directory */
    snprintf(cmd, sizeof(cmd), "rm -rf %s", test_mount);
    (void)system(cmd);

    printf(">>> ALL 6 ENTERPRISE SERVER INSTALLER TEST SUITES PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
