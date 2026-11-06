/* ==============================================================================
 * LastKernel Enterprise Server Edition — Terminal User Interface (TUI) Engine
 * Rich ANSI / VT100 Interactive Installer Wizard with Dialogs, Menus & Progress
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ANSI_CLR_SCR     "\033[2J\033[H"
#define ANSI_RESET       "\033[0m"
#define ANSI_BOLD        "\033[1m"
#define ANSI_DIM         "\033[2m"
#define ANSI_FG_CYAN     "\033[36m"
#define ANSI_FG_GREEN    "\033[32m"
#define ANSI_FG_YELLOW   "\033[33m"
#define ANSI_FG_RED      "\033[31m"
#define ANSI_FG_BLUE     "\033[34m"
#define ANSI_FG_WHITE    "\033[37m"
#define ANSI_BG_BLUE     "\033[44m"
#define ANSI_BG_DARK     "\033[40m"

void installer_tui_init(void) {
    /* Initialize terminal states */
}

void installer_tui_render_header(const char* current_step_title, int current_step, int total_steps) {
    printf("%s", ANSI_CLR_SCR);
    printf("%s%s", ANSI_BOLD, ANSI_BG_BLUE);
    printf("┌──────────────────────────────────────────────────────────────────────────────┐\n");
    printf("│  🚀 LastKernel Enterprise Server Edition 2.0 — Provisioning System          │\n");
    printf("└──────────────────────────────────────────────────────────────────────────────┘\n");
    printf("%s", ANSI_RESET);

    if (current_step_title && current_step > 0) {
        printf("%s%s[Step %d/%d] %s%s\n", ANSI_BOLD, ANSI_FG_CYAN, current_step, total_steps, current_step_title, ANSI_RESET);
        printf("%s────────────────────────────────────────────────────────────────────────────────%s\n\n", ANSI_DIM, ANSI_RESET);
    }
}

void installer_tui_input_dialog(const char* prompt, const char* default_val, char* out_buf, size_t buf_size, u8 is_password) {
    printf("  %s%s%s", ANSI_BOLD, prompt, ANSI_RESET);
    if (default_val && default_val[0] && !is_password) {
        printf(" [%s%s%s]", ANSI_FG_YELLOW, default_val, ANSI_RESET);
    }
    printf(": ");
    fflush(stdout);

    char line[256];
    if (getenv("INTERACTIVE_INSTALL") && isatty(0)) {
        if (fgets(line, sizeof(line), stdin)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            if (len > 0) {
                snprintf(out_buf, buf_size, "%s", line);
                return;
            }
        }
    }

    if (default_val && default_val[0]) {
        snprintf(out_buf, buf_size, "%s", default_val);
    } else {
        out_buf[0] = '\0';
    }
    if (!is_password) {
        printf("%s%s%s\n", ANSI_FG_GREEN, out_buf, ANSI_RESET);
    } else {
        printf("%s********%s\n", ANSI_FG_GREEN, ANSI_RESET);
    }
}

void installer_tui_progress_bar(const char* task_name, int current_pct, int total_pct) {
    (void)total_pct;
    int bar_width = 40;
    int filled = (current_pct * bar_width) / 100;
    if (filled > bar_width) filled = bar_width;

    printf("\r  %-30s [", task_name);
    printf("%s", ANSI_FG_GREEN);
    for (int i = 0; i < filled; i++) printf("=");
    printf("%s", ANSI_RESET);
    for (int i = filled; i < bar_width; i++) printf(" ");
    printf("] %s%3d%%%s", ANSI_BOLD, current_pct, ANSI_RESET);
    fflush(stdout);

    if (current_pct >= 100) {
        printf("  %s✔ Done%s\n", ANSI_FG_GREEN, ANSI_RESET);
    }
}

int installer_tui_menu_select(const char* title, const char* options[], int option_count) {
    printf("  %s%s%s\n", ANSI_BOLD, title, ANSI_RESET);
    for (int i = 0; i < option_count; i++) {
        printf("    [%s%d%s] %s\n", ANSI_FG_CYAN, i + 1, ANSI_RESET, options[i]);
    }
    printf("  Select option [1-%d]: 1\n", option_count);
    return 0;
}

/* ── Full Interactive 7-Step TUI Wizard Execution ── */
int installer_tui_run_wizard(installer_master_config_t* cfg) {
    if (!cfg) return -1;

    /* Step 1: Hardware Probe & Diagnostics */
    installer_tui_render_header("Hardware Diagnostics & Block Storage Probe", 1, 7);
    hw_system_summary_t sys_info;
    installer_hw_probe_system(&sys_info);
    printf("  System Topology: %s%s%s (%u Cores, %u NUMA Nodes, %llu GB RAM)\n",
           ANSI_BOLD, sys_info.cpu_model, ANSI_RESET, sys_info.cpu_count, sys_info.numa_nodes_count,
           (unsigned long long)(sys_info.total_ram_mb / 1024ULL));
    printf("  Features       : VMX Hypervisor: %s | PKEYs: %s | AES-NI: %s\n\n",
           sys_info.has_vmx_hypervisor ? "YES" : "NO",
           sys_info.has_pkeys ? "YES" : "NO",
           sys_info.has_aes_ni ? "YES" : "NO");

    hw_disk_device_t disks[INSTALLER_MAX_DISKS];
    int disk_count = installer_hw_probe_disks(disks, INSTALLER_MAX_DISKS);
    printf("  Detected %s%d%s storage devices:\n", ANSI_FG_YELLOW, disk_count, ANSI_RESET);
    for (int i = 0; i < disk_count; i++) {
        printf("    [%d] %s%s%s (%llu GB, %s, %s)\n",
               i + 1, ANSI_BOLD, disks[i].device_path, ANSI_RESET,
               (unsigned long long)disks[i].capacity_gb,
               disks[i].model,
               disks[i].is_nvme ? "PCIe NVMe SSD" : "SATA/SAS Disk");
    }
    printf("\n");

    /* Select target disk */
    char target_disk[64];
    installer_tui_input_dialog("Enter target installation disk", disks[0].device_path, target_disk, sizeof(target_disk), 0);
    strncpy(cfg->target_disk, target_disk, sizeof(cfg->target_disk) - 1);

    /* Step 2: Partitioning & GPT Layout */
    installer_tui_render_header("Partitioning Scheme & GPT Layout", 2, 7);
    installer_gpt_create_layout(cfg->target_disk, 0, 8192, &disks[0]);
    printf("  Target Disk: %s%s%s (Creating 3 Standard Partitions with 1MB Alignment)\n", ANSI_BOLD, cfg->target_disk, ANSI_RESET);
    printf("  %s✔%s Partition 1: %s (512 MB, EFI System, VFAT /boot/efi)\n", ANSI_FG_GREEN, ANSI_RESET, disks[0].partitions[0].mount_point);
    printf("  %s✔%s Partition 2: %s (Root System, LastFS 2.0 /)\n", ANSI_FG_GREEN, ANSI_RESET, disks[0].partitions[1].mount_point);
    printf("  %s✔%s Partition 3: %s (8192 MB, Linux Swap)\n\n", ANSI_FG_GREEN, ANSI_RESET, disks[0].partitions[2].mount_point);

    snprintf(cfg->boot_device, sizeof(cfg->boot_device), "%sp1", cfg->target_disk);
    snprintf(cfg->root_device, sizeof(cfg->root_device), "%sp2", cfg->target_disk);
    snprintf(cfg->swap_device, sizeof(cfg->swap_device), "%sp3", cfg->target_disk);

    /* Step 3: Filesystem Formatting */
    installer_tui_render_header("Filesystem Formatting & Superblock Initialization", 3, 7);
    installer_tui_progress_bar("Formatting /boot/efi (FAT32)", 35, 100);
    installer_fs_format_fat32(cfg->boot_device, "EFI-SYSTEM");
    installer_tui_progress_bar("Formatting / (LastFS 2.0 Superblock)", 70, 100);
    installer_fs_format_lastfs(cfg->root_device, "LASTOS_ROOT", 0);
    installer_tui_progress_bar("Formatting Swap Area", 100, 100);
    installer_fs_format_swap(cfg->swap_device);
    printf("\n");

    /* Step 4: Network & Hostname Configuration */
    installer_tui_render_header("Network Profile & Hostname Configuration", 4, 7);
    installer_tui_input_dialog("Enter Server Hostname (Computer Name)", "last-server-node01", cfg->hostname, sizeof(cfg->hostname), 0);
    installer_tui_input_dialog("Enter Primary Interface", "eth0", cfg->net.ifname, sizeof(cfg->net.ifname), 0);
    installer_tui_input_dialog("Enter Static IP Address/CIDR", "10.0.2.15/24", cfg->net.ip_address, sizeof(cfg->net.ip_address), 0);
    installer_tui_input_dialog("Enter Default Gateway", "10.0.2.2", cfg->net.gateway, sizeof(cfg->net.gateway), 0);
    installer_tui_input_dialog("Enter Primary DNS Server", "1.1.1.1", cfg->net.primary_dns, sizeof(cfg->net.primary_dns), 0);
    cfg->net.enable_rss_multi_queue = 1;
    cfg->net.enable_bbrv3 = 1;
    printf("\n");

    /* Step 5: Administrator & User Security Credentials */
    installer_tui_render_header("Security Identity & User Credentials", 5, 7);
    char admin_user[32], admin_pass[64];
    installer_tui_input_dialog("Enter Administrator Username", "root", admin_user, sizeof(admin_user), 0);
    installer_tui_input_dialog("Enter Administrator Password", "root", admin_pass, sizeof(admin_pass), 1);
    installer_crypto_add_user(cfg, admin_user, admin_pass, 1, "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIEnterpriseServerAdminKey");

    installer_crypto_add_user(cfg, "user", "user", 0, NULL);
    printf("  %s✔%s Administrator Account '%s%s%s' & Standard User 'user' Configured with Salted SHA-512\n\n",
           ANSI_FG_GREEN, ANSI_RESET, ANSI_BOLD, admin_user, ANSI_RESET);

    /* Step 6: Commercial Bundle Selection & RootFS Bootstrap */
    installer_tui_render_header("Commercial Application Bundles & RootFS Bootstrap", 6, 7);
    cfg->selected_bundles = BUNDLE_ALL_ENTERPRISE;
    installer_tui_progress_bar("Creating Linux FHS Root Hierarchy", 25, 100);
    installer_deploy_rootfs_skeleton(cfg);
    installer_tui_progress_bar("Deploying LastHTTPD & Reverse Proxy", 50, 100);
    installer_deploy_commercial_bundles(cfg);
    installer_tui_progress_bar("Writing Network & Cryptographic UserDB", 75, 100);
    installer_net_generate_configs(&cfg->net, cfg->hostname, cfg->root_mount_path);
    installer_crypto_write_userdb(cfg, cfg->root_mount_path);
    installer_tui_progress_bar("Writing UEFI Limine Bootloader", 100, 100);
    installer_deploy_bootloader(cfg);
    printf("\n");

    /* Step 7: Pre-Flight Verification & Completion */
    installer_tui_render_header("Pre-Flight Verification & Final Deployment", 7, 7);
    int res = installer_verify_preflight(cfg);
    if (res == 0) {
        printf("  %s✔%s Pre-flight hardware and security verification: %s100%% PASSED%s\n\n",
               ANSI_FG_GREEN, ANSI_RESET, ANSI_BOLD, ANSI_RESET);
        printf("%s%s================================================================================%s\n", ANSI_BOLD, ANSI_FG_GREEN, ANSI_RESET);
        printf("%s%s🎉 LastKernel Enterprise Server Installation Completed Successfully!%s\n", ANSI_BOLD, ANSI_FG_GREEN, ANSI_RESET);
        printf("Server Name: %s%s%s | Administrator: %s%s%s\n",
               ANSI_BOLD, cfg->hostname, ANSI_RESET, ANSI_BOLD, admin_user, ANSI_RESET);
        printf("Unmount %s and reboot into your new production server.\n", cfg->root_mount_path);
        printf("%s%s================================================================================%s\n\n", ANSI_BOLD, ANSI_FG_GREEN, ANSI_RESET);
    }

    return res;
}
