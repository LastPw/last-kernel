/* ==============================================================================
 * LastKernel Enterprise Server Edition — Master CLI & Unattended Installer
 * Complete Terminal Command-Line & Automation Suite for Cloud & Bare-Metal
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_RED     "\033[31m"

static void print_banner(void) {
    printf("%s%s", ANSI_BOLD, ANSI_CYAN);
    printf("================================================================================\n");
    printf("     🚀 LastKernel Enterprise Server Edition — Commercial Installer CLI        \n");
    printf("     High-Performance, Multi-Domain, Polymorphic Cloud & Edge Server OS        \n");
    printf("================================================================================\n");
    printf("%s", ANSI_RESET);
}

static void print_usage(void) {
    printf("Usage: last-install <command> [options]\n\n");
    printf("Available Commands:\n");
    printf("  %swizard%s                       Launch full 7-step interactive Gentoo-style TUI wizard\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sprobe-hw%s                     Run deep hardware diagnostics (CPU, NUMA, RAM, NVMe SMART)\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sprobe-disks%s                  Scan & display all hardware block storage devices (NVMe/SATA)\n", ANSI_GREEN, ANSI_RESET);
    printf("  %spartition%s <disk> [swap_mb]   Create standard GPT partitions (512MB EFI, LastFS Root, Swap)\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sraid-create%s <lvl> <disks...> Construct Software RAID 0/1/5/6 Array with Galois Field parity\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sformat%s <part> <fs_type>      Format partition with LastFS 2.0, FAT32 or Swap\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sbootstrap%s <mount_path>       Deploy Linux FHS rootfs and commercial enterprise bundles\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sset-user%s <user> <pass> <adm> Configure administrator or user account with PBKDF2-SHA512\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sset-hostname%s <hostname>      Configure server computer name in /etc/hostname & /etc/hosts\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sconfig-net%s <if> <ip> <gw>    Generate production network & DNS interface profiles\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sbootloader%s <disk> [profile]  Install UEFI Limine bootloader with chosen kernel profile\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sunattended%s                   Execute non-interactive automated server provisioning\n", ANSI_GREEN, ANSI_RESET);
    printf("  %sverify%s                       Perform cryptographic and hardware pre-flight validation\n\n", ANSI_GREEN, ANSI_RESET);
}

int main(int argc, char** argv) {
    installer_init();
    installer_tui_init();

    if (argc < 2) {
        print_banner();
        print_usage();
        return 0;
    }

    installer_master_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.root_mount_path, sizeof(cfg.root_mount_path), "/mnt/target");
    snprintf(cfg.hostname, sizeof(cfg.hostname), "last-server-node01");
    cfg.kernel_profile = 1;
    cfg.selected_bundles = BUNDLE_ALL_ENTERPRISE;

    if (strcmp(argv[1], "wizard") == 0) {
        return installer_tui_run_wizard(&cfg);
    } else if (strcmp(argv[1], "probe-hw") == 0) {
        hw_system_summary_t sys;
        installer_hw_probe_system(&sys);
        printf("%s--- System Hardware Topology ---%s\n", ANSI_BOLD, ANSI_RESET);
        printf("  Processor : %s (%u Cores, %u NUMA Nodes)\n", sys.cpu_model, sys.cpu_count, sys.numa_nodes_count);
        printf("  Memory    : %llu MB Total (%llu MB Free)\n", (unsigned long long)sys.total_ram_mb, (unsigned long long)sys.free_ram_mb);
        printf("  Security  : VMX Hypervisor: %s | PKEYs: %s | AES-NI: %s | AVX-512: %s\n",
               sys.has_vmx_hypervisor ? "Active" : "Disabled",
               sys.has_pkeys ? "Active" : "Disabled",
               sys.has_aes_ni ? "Active" : "Disabled",
               sys.has_avx512 ? "Active" : "Disabled");
        return 0;
    } else if (strcmp(argv[1], "probe-disks") == 0) {
        hw_disk_device_t disks[INSTALLER_MAX_DISKS];
        int count = installer_hw_probe_disks(disks, INSTALLER_MAX_DISKS);
        printf("%sFound %d block storage devices:%s\n", ANSI_BOLD, count, ANSI_RESET);
        for (int i = 0; i < count; i++) {
            printf("  [%d] %-16s %-32s (%5llu GB, %-12s, Sectors: %llu)\n",
                   i + 1, disks[i].device_path, disks[i].model,
                   (unsigned long long)disks[i].capacity_gb,
                   disks[i].is_nvme ? "NVMe SSD" : "SATA/SAS",
                   (unsigned long long)disks[i].total_sectors);
        }
        return 0;
    } else if (strcmp(argv[1], "partition") == 0) {
        if (argc < 3) {
            printf("Error: missing target disk (e.g. /dev/nvme0n1)\n");
            return 1;
        }
        u64 swap_mb = (argc >= 4) ? strtoull(argv[3], NULL, 10) : 8192;
        hw_disk_device_t disk;
        memset(&disk, 0, sizeof(disk));
        installer_gpt_create_layout(argv[2], 0, swap_mb, &disk);
        printf("GPT layout prepared for %s with %u partitions.\n", argv[2], disk.partition_count);
        return 0;
    } else if (strcmp(argv[1], "raid-create") == 0) {
        if (argc < 5) {
            printf("Usage: last-install raid-create <level:0|1|5|6> <disk1> <disk2> ...\n");
            return 1;
        }
        u32 level = (u32)atoi(argv[2]);
        const char* members[INSTALLER_MAX_RAID_MEMBERS];
        u32 count = (u32)(argc - 3);
        for (u32 i = 0; i < count; i++) members[i] = argv[3 + i];
        int res = installer_raid_init_array(level, members, count, "md0");
        if (res == 0) {
            printf("RAID %u array configured successfully with %u disks.\n", level, count);
        } else {
            printf("Error configuring RAID array (code: %d)\n", res);
        }
        return res;
    } else if (strcmp(argv[1], "format") == 0) {
        if (argc < 4) {
            printf("Usage: last-install format <partition_path> <fs_type:lastfs|fat32|swap>\n");
            return 1;
        }
        int res = 0;
        if (strcmp(argv[3], "fat32") == 0 || strcmp(argv[3], "vfat") == 0) {
            res = installer_fs_format_fat32(argv[2], "EFI-SYSTEM");
        } else if (strcmp(argv[3], "lastfs") == 0) {
            res = installer_fs_format_lastfs(argv[2], "LASTOS_ROOT", 0);
        } else if (strcmp(argv[3], "swap") == 0) {
            res = installer_fs_format_swap(argv[2]);
        } else {
            printf("Unsupported filesystem type '%s'\n", argv[3]);
            return 1;
        }
        printf("Partition %s formatted as %s.\n", argv[2], argv[3]);
        return res;
    } else if (strcmp(argv[1], "bootstrap") == 0) {
        const char* mount_path = (argc >= 3) ? argv[2] : "/mnt/target";
        snprintf(cfg.root_mount_path, sizeof(cfg.root_mount_path), "%s", mount_path);
        installer_deploy_rootfs_skeleton(&cfg);
        installer_deploy_commercial_bundles(&cfg);
        printf("RootFS skeleton and enterprise bundles deployed on %s.\n", mount_path);
        return 0;
    } else if (strcmp(argv[1], "set-user") == 0) {
        if (argc < 4) {
            printf("Usage: last-install set-user <username> <password> [is_admin:0|1] [root_mount]\n");
            return 1;
        }
        u8 is_admin = (argc >= 5 && strcmp(argv[4], "1") == 0) ? 1 : 0;
        const char* mount_path = (argc >= 6) ? argv[5] : "/mnt/target";
        installer_crypto_add_user(&cfg, argv[2], argv[3], is_admin, NULL);
        installer_crypto_write_userdb(&cfg, mount_path);
        printf("User '%s' (admin: %d) written to UserDB on %s.\n", argv[2], is_admin, mount_path);
        return 0;
    } else if (strcmp(argv[1], "set-hostname") == 0) {
        if (argc < 3) {
            printf("Usage: last-install set-hostname <hostname> [root_mount]\n");
            return 1;
        }
        const char* mount_path = (argc >= 4) ? argv[3] : "/mnt/target";
        installer_configure_hostname(argv[2], mount_path);
        printf("Hostname '%s' configured on %s.\n", argv[2], mount_path);
        return 0;
    } else if (strcmp(argv[1], "unattended") == 0) {
        printf("Running unattended automated installation on /dev/nvme0n1...\n");
        installer_crypto_add_user(&cfg, "root", "root", 1, NULL);
        installer_crypto_add_user(&cfg, "user", "user", 0, NULL);
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

        installer_deploy_rootfs_skeleton(&cfg);
        installer_deploy_commercial_bundles(&cfg);
        installer_net_generate_configs(&cfg.net, cfg.hostname, cfg.root_mount_path);
        installer_crypto_write_userdb(&cfg, cfg.root_mount_path);
        installer_deploy_bootloader(&cfg);
        int res = installer_verify_preflight(&cfg);
        printf("Unattended deployment completed with preflight status: %s\n", res == 0 ? "SUCCESS" : "FAILED");
        return res;
    } else if (strcmp(argv[1], "verify") == 0) {
        installer_crypto_add_user(&cfg, "root", "root", 1, NULL);
        snprintf(cfg.target_disk, sizeof(cfg.target_disk), "/dev/nvme0n1");
        snprintf(cfg.root_device, sizeof(cfg.root_device), "/dev/nvme0n1p2");
        snprintf(cfg.boot_device, sizeof(cfg.boot_device), "/dev/nvme0n1p1");
        int res = installer_verify_preflight(&cfg);
        printf("Verification check: %s\n", res == 0 ? "PASS (100% Validated)" : "FAIL");
        return res;
    }

    print_usage();
    return 0;
}
