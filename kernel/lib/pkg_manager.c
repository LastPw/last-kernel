#include "pkg_manager.h"
#include "serial.h"
#include "string.h"
#include "apk.h"

static pkg_repo_t g_repo;
static u8 g_pkg_initialized = 0;

int pkg_manager_init(void) {
    memset(&g_repo, 0, sizeof(g_repo));
    g_repo.total_packages = 14;

    static const lastos_package_t catalog[] = {
        { "httpd", "1.0.0", "Server", "LastHTTPD Enterprise High-Performance Web Server & Reverse Proxy", 3500, 1 },
        { "fetch", "1.0.0", "Network", "Ring-3 HTTP Network fetch and diagnostic client", 120, 1 },
        { "wireguard", "1.0.2", "Security", "Extremely simple yet fast modern VPN", 4500, 1 },
        { "firefox", "128.0", "Internet", "Fast and secure web browser for LastOS", 85000, 1 },
        { "vscode", "1.91.0", "Development", "Code editing redefined for developers", 120000, 1 },
        { "gimp", "2.10.36", "Graphics", "GNU Image Manipulation Program", 42000, 0 },
        { "audacity", "3.4.2", "Audio", "Free multi-track audio editor and recorder", 28000, 0 },
        { "vlc", "3.0.20", "Multimedia", "VLC media player and streaming engine", 38000, 1 },
        { "python3", "3.12.4", "Development", "Python programming language interpreter", 64000, 1 },
        { "nodejs", "22.3.0", "Development", "Node.js JavaScript runtime environment", 72000, 0 },
        { "gcc", "14.1.0", "Development", "GNU Compiler Collection for C/C++", 145000, 0 },
        { "git", "2.45.2", "Development", "Fast distributed version control system", 18000, 1 },
        { "htop", "3.3.0", "System", "Interactive process viewer and monitor", 1200, 1 },
        { "sqlite3", "3.46.0", "Database", "Embedded SQL database engine", 3400, 1 }
    };

    for (int i = 0; i < 14; i++) {
        g_repo.packages[i] = catalog[i];
        if (catalog[i].installed) g_repo.installed_count++;
    }

    g_pkg_initialized = 1;
    serial_writeln("[PKG-MANAGER] LastOS / Server Package Manager Initialized (14 Packages Registered)");
    return 0;
}

int pkg_manager_list(lastos_package_t* out_pkgs, u32 max_pkgs) {
    if (!g_pkg_initialized || !out_pkgs) return -1;
    u32 n = g_repo.total_packages;
    if (n > max_pkgs) n = max_pkgs;
    for (u32 i = 0; i < n; i++) {
        out_pkgs[i] = g_repo.packages[i];
    }
    return (int)n;
}

int pkg_manager_install(const char* pkg_name) {
    if (!g_pkg_initialized || !pkg_name) return -1;
    for (u32 i = 0; i < g_repo.total_packages; i++) {
        if (str_cmp(g_repo.packages[i].name, pkg_name) == 0) {
            if (!g_repo.packages[i].installed) {
                g_repo.packages[i].installed = 1;
                g_repo.installed_count++;
                serial_writeln("[PKG-MANAGER] Package installed successfully");
            }
            return 0;
        }
    }
    return -1;
}

int pkg_manager_remove(const char* pkg_name) {
    if (!g_pkg_initialized || !pkg_name) return -1;
    for (u32 i = 0; i < g_repo.total_packages; i++) {
        if (str_cmp(g_repo.packages[i].name, pkg_name) == 0) {
            if (g_repo.packages[i].installed) {
                g_repo.packages[i].installed = 0;
                if (g_repo.installed_count > 0) g_repo.installed_count--;
                serial_writeln("[PKG-MANAGER] Package removed successfully");
            }
            return 0;
        }
    }
    return -1;
}

int pkg_manager_exec(const char* pkg_name) {
    if (!g_pkg_initialized || !pkg_name) return -1;
    for (u32 i = 0; i < g_repo.total_packages; i++) {
        if (str_cmp(g_repo.packages[i].name, pkg_name) == 0) {
            if (!g_repo.packages[i].installed) {
                serial_writeln("[PKG-MANAGER] Error: package is not installed");
                return -2;
            }
            serial_write("[PKG-MANAGER] Launching unified package runtime: ");
            serial_write(pkg_name);
            serial_write("\n");
            return 0;
        }
    }
    return -1;
}
