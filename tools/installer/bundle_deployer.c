/* ==============================================================================
 * LastKernel Enterprise Server Edition — RootFS & Commercial Bundle Deployer
 * Full System Deployment: Linux FHS Skeleton, LastHTTPD, WireGuard, Limine & Fstab
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int ensure_dir_p(const char* dir_path) {
    char temp[512];
    snprintf(temp, sizeof(temp), "%s", dir_path);
    size_t len = strlen(temp);
    if (len == 0) return 0;
    if (temp[len - 1] == '/') temp[len - 1] = '\0';

    for (char* p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp, 0755);
            *p = '/';
        }
    }
    return mkdir(temp, 0755);
}

static int write_file_str(const char* file_path, const char* content) {
    FILE* f = fopen(file_path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    return 0;
}

/* ── Deploy Linux FHS RootFS Hierarchy ── */
int installer_deploy_rootfs_skeleton(const installer_master_config_t* cfg) {
    if (!cfg || !cfg->root_mount_path[0]) return -1;

    const char* m = cfg->root_mount_path;
    char path[512];

    const char* dirs[] = {
        "bin", "sbin", "usr/bin", "usr/sbin", "usr/lib", "usr/include",
        "etc", "etc/last-httpd", "etc/wireguard", "etc/lastpkg", "etc/skel",
        "var", "var/log", "var/run", "var/www/html", "var/lib/lastpkg",
        "lib", "lib64", "dev", "proc", "sys", "tmp", "run", "opt",
        "boot", "boot/efi", "root", "root/.ssh", "home"
    };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", m, dirs[i]);
        ensure_dir_p(path);
    }

    /* 1. /etc/os-release */
    snprintf(path, sizeof(path), "%s/etc/os-release", m);
    write_file_str(path,
        "NAME=\"LastKernel Enterprise Server\"\n"
        "VERSION=\"2.0-Enterprise\"\n"
        "ID=\"lastos\"\n"
        "ID_LIKE=\"linux\"\n"
        "VERSION_ID=\"2.0\"\n"
        "PRETTY_NAME=\"LastKernel Enterprise Server Edition 2.0 (x86_64)\"\n"
        "HOME_URL=\"https://axior.corp/last-kernel\"\n");

    /* 2. /etc/fstab */
    snprintf(path, sizeof(path), "%s/etc/fstab", m);
    char fstab_buf[1024];
    snprintf(fstab_buf, sizeof(fstab_buf),
        "# /etc/fstab: static file system information\n"
        "# <file system>    <mount point>   <type>      <options>               <dump>  <pass>\n"
        "%-18s /               lastfs      defaults,noatime,rw     0       1\n"
        "%-18s /boot/efi       vfat        defaults,noatime        0       2\n"
        "%-18s none            swap        sw                      0       0\n"
        "tmpfs              /tmp            tmpfs       defaults,nosuid,nodev   0       0\n"
        "proc               /proc           proc        defaults                0       0\n"
        "sysfs              /sys            sysfs       defaults                0       0\n",
        cfg->root_device[0] ? cfg->root_device : "/dev/nvme0n1p2",
        cfg->boot_device[0] ? cfg->boot_device : "/dev/nvme0n1p1",
        cfg->swap_device[0] ? cfg->swap_device : "/dev/nvme0n1p3");
    write_file_str(path, fstab_buf);

    /* 3. /etc/sysctl.conf (High-Performance Server Tuning) */
    snprintf(path, sizeof(path), "%s/etc/sysctl.conf", m);
    write_file_str(path,
        "# ==============================================================================\n"
        "# LastKernel Enterprise High-Throughput Network & Kernel Tuning\n"
        "# ==============================================================================\n"
        "net.core.somaxconn = 65535\n"
        "net.ipv4.tcp_congestion_control = bbr\n"
        "net.ipv4.tcp_slow_start_after_idle = 0\n"
        "net.ipv4.tcp_syncookies = 1\n"
        "net.ipv4.tcp_tw_reuse = 1\n"
        "net.core.rmem_max = 67108864\n"
        "net.core.wmem_max = 67108864\n"
        "vm.swappiness = 10\n"
        "vm.dirty_ratio = 15\n"
        "vm.dirty_background_ratio = 5\n");

    /* 4. /etc/profile & /etc/issue */
    snprintf(path, sizeof(path), "%s/etc/profile", m);
    write_file_str(path,
        "export PATH=/bin:/sbin:/usr/bin:/usr/sbin:/opt/bin\n"
        "export PS1='\\u@\\h:\\w\\$ '\n"
        "export TERM=xterm-256color\n");

    snprintf(path, sizeof(path), "%s/etc/issue", m);
    write_file_str(path,
        "LastKernel Enterprise Server Edition 2.0 (\\l)\n\n");

    return 0;
}

/* ── Deploy Enterprise Commercial Application Bundles ── */
int installer_deploy_commercial_bundles(const installer_master_config_t* cfg) {
    if (!cfg || !cfg->root_mount_path[0]) return -1;

    const char* m = cfg->root_mount_path;
    char path[512];

    /* 1. LastHTTPD Web Server & Reverse Proxy */
    if (cfg->selected_bundles & BUNDLE_LAST_HTTPD) {
        snprintf(path, sizeof(path), "%s/etc/last-httpd/last-httpd.conf", m);
        write_file_str(path,
            "# ==============================================================================\n"
            "# LastHTTPD Enterprise Production Reverse Proxy & Web Server\n"
            "# ==============================================================================\n"
            "worker_processes auto;\n"
            "events {\n"
            "    worker_connections 65535;\n"
            "    use io_uring;\n"
            "}\n"
            "http {\n"
            "    sendfile on;\n"
            "    tcp_nopush on;\n"
            "    tcp_nodelay on;\n"
            "    keepalive_timeout 65;\n"
            "    bbrv3_active on;\n"
            "    waf_protection on;\n"
            "\n"
            "    server {\n"
            "        listen 80;\n"
            "        server_name _;\n"
            "        return 301 https://$host$request_uri;\n"
            "    }\n"
            "\n"
            "    server {\n"
            "        listen 443 ssl http2;\n"
            "        server_name _;\n"
            "        ssl_certificate /etc/last-httpd/certs/server.crt;\n"
            "        ssl_certificate_key /etc/last-httpd/certs/server.key;\n"
            "        ssl_protocols TLSv1.3;\n"
            "        root /var/www/html;\n"
            "        index index.html;\n"
            "\n"
            "        location /metrics {\n"
            "            stub_status on;\n"
            "        }\n"
            "    }\n"
            "}\n");

        snprintf(path, sizeof(path), "%s/var/www/html/index.html", m);
        write_file_str(path,
            "<!DOCTYPE html>\n<html lang='en'>\n<head>\n"
            "<meta charset='UTF-8'><title>LastKernel Enterprise Server</title>\n"
            "<style>\n"
            "body { font-family: 'Segoe UI', system-ui, sans-serif; background: #0b0f19; color: #f3f4f6; margin: 0; padding: 60px 20px; text-align: center; }\n"
            ".card { background: #111827; border: 1px solid #1f2937; border-radius: 12px; max-width: 700px; margin: 0 auto; padding: 40px; box-shadow: 0 20px 25px -5px rgba(0,0,0,0.5); }\n"
            "h1 { color: #38bdf8; margin-top: 0; }\n"
            ".badge { display: inline-block; background: #0369a1; color: #e0f2fe; padding: 4px 12px; border-radius: 9999px; font-size: 13px; font-weight: 600; margin-bottom: 20px; }\n"
            ".grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; text-align: left; margin-top: 30px; }\n"
            ".item { background: #1f2937; padding: 15px; border-radius: 8px; font-size: 14px; }\n"
            ".val { font-weight: 600; color: #4ade80; float: right; }\n"
            "</style>\n</head>\n<body>\n"
            "<div class='card'>\n"
            "  <div class='badge'>COMMERCIAL ENTERPRISE EDITION</div>\n"
            "  <h1>🚀 LastKernel Enterprise Server</h1>\n"
            "  <p>Production Cloud & Edge Operating System is Online and Healthy.</p>\n"
            "  <div class='grid'>\n"
            "    <div class='item'>Web Server <span class='val'>LastHTTPD 2.0</span></div>\n"
            "    <div class='item'>Network Engine <span class='val'>RSS + BBRv3</span></div>\n"
            "    <div class='item'>Filesystem <span class='val'>LastFS 2.0</span></div>\n"
            "    <div class='item'>Hypervisor <span class='val'>Intel VT-x MicroVM</span></div>\n"
            "  </div>\n"
            "</div>\n</body>\n</html>\n");
    }

    /* 2. WireGuard Mesh Config */
    if (cfg->selected_bundles & BUNDLE_WIREGUARD_MESH) {
        snprintf(path, sizeof(path), "%s/etc/wireguard/wg0.conf", m);
        write_file_str(path,
            "[Interface]\n"
            "Address = 10.100.0.1/24\n"
            "ListenPort = 51820\n"
            "PrivateKey = AUTO_GENERATED_AT_BOOT\n"
            "SaveConfig = true\n");
    }

    return 0;
}

/* ── Deploy Limine UEFI/BIOS Bootloader Configuration ── */
int installer_deploy_bootloader(const installer_master_config_t* cfg) {
    if (!cfg || !cfg->root_mount_path[0]) return -1;

    const char* m = cfg->root_mount_path;
    char path[512];

    const char* prof = "server";
    if (cfg->kernel_profile == 2) prof = "microvm";
    else if (cfg->kernel_profile == 3) prof = "realtime";

    char limine_cfg[2048];
    snprintf(limine_cfg, sizeof(limine_cfg),
        "# ==============================================================================\n"
        "# LastKernel Limine Bootloader Specification\n"
        "# ==============================================================================\n"
        "TIMEOUT=3\n"
        "DEFAULT_ENTRY=1\n"
        "INTERFACE_BRANDING=LastKernel Enterprise Server Edition 2.0\n\n"
        ":LastKernel Enterprise Server Edition (Standard Production)\n"
        "PROTOCOL=limine\n"
        "KERNEL_PATH=boot:///boot/kernel.elf\n"
        "CMDLINE=profile=%s root=%s quiet bbrv3=1 numa=auto pkeys=on rss=8\n\n"
        ":LastKernel Enterprise Server (Cloud MicroVM Hypervisor)\n"
        "PROTOCOL=limine\n"
        "KERNEL_PATH=boot:///boot/kernel.elf\n"
        "CMDLINE=profile=microvm root=%s quiet vmx=on numa=auto\n\n"
        ":LastKernel Enterprise Server (Rescue / Single-User Emergency Shell)\n"
        "PROTOCOL=limine\n"
        "KERNEL_PATH=boot:///boot/kernel.elf\n"
        "CMDLINE=profile=rescue single root=%s init=/bin/sh\n",
        prof,
        cfg->root_device[0] ? cfg->root_device : "/dev/nvme0n1p2",
        cfg->root_device[0] ? cfg->root_device : "/dev/nvme0n1p2",
        cfg->root_device[0] ? cfg->root_device : "/dev/nvme0n1p2");

    snprintf(path, sizeof(path), "%s/boot/limine.cfg", m);
    return write_file_str(path, limine_cfg);
}

/* ── Pre-flight Verification Check ── */
int installer_verify_preflight(const installer_master_config_t* cfg) {
    if (!cfg) return -1;
    if (!cfg->target_disk[0]) return -2;
    if (!cfg->hostname[0]) return -3;
    if (cfg->user_count == 0) return -4;

    /* Verify root user exists */
    int has_root = 0;
    for (u32 i = 0; i < cfg->user_count; i++) {
        if (cfg->users[i].is_admin) {
            has_root = 1;
            break;
        }
    }
    if (!has_root) return -5;

    return 0;
}
