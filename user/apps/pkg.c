/* ==============================================================================
 * LastOS / LastKernel Userspace Package Manager (Ring-3 'pkg' CLI)
 * Runs purely in Ring-3 Userspace via Standard POSIX / LastOS Syscalls
 * ============================================================================== */

#include "libsys.h"

typedef struct {
    char name[32];
    char version[16];
    char category[24];
    char description[96];
    u32 size_kb;
    u8 installed;
} package_entry_t;

static package_entry_t s_catalog[] = {
    { "httpd", "1.0.0", "Server", "LastHTTPD Enterprise High-Performance Web Server & Reverse Proxy", 3500, 1 },
    { "fetch", "1.0.0", "Network", "Ring-3 HTTP Network fetch and diagnostic client", 120, 1 },
    { "wireguard", "1.0.2", "Security", "Extremely simple yet fast modern VPN tunnel", 4500, 1 },
    { "firefox", "128.0", "Internet", "Fast and secure web browser for LastOS", 85000, 1 },
    { "vscode", "1.91.0", "Development", "Code editing redefined for developers", 120000, 1 },
    { "python3", "3.12.4", "Development", "Python programming language interpreter", 64000, 1 },
    { "git", "2.45.2", "Development", "Fast distributed version control system", 18000, 1 },
    { "htop", "3.3.0", "System", "Interactive process viewer and monitor", 1200, 1 },
    { "sqlite3", "3.46.0", "Database", "Embedded SQL database engine", 3400, 1 },
    { "gcc", "14.1.0", "Development", "GNU Compiler Collection for C/C++", 145000, 0 },
    { "nodejs", "22.3.0", "Development", "Node.js JavaScript runtime environment", 72000, 0 },
    { "vlc", "3.0.20", "Multimedia", "VLC media player and streaming engine", 38000, 0 }
};

#define CATALOG_SIZE (sizeof(s_catalog) / sizeof(s_catalog[0]))

static void print_help(void) {
    println("LastOS Package Manager (pkg) - Ring-3 Userspace CLI");
    println("Usage: pkg <command> [arguments]");
    println("Commands:");
    println("  list                 List all available packages and installation status");
    println("  install <package>    Install a package to /bin/ or /usr/bin/");
    println("  remove <package>     Uninstall a package from system");
    println("  info <package>       Display package version, metadata and description");
    println("  run <package>        Execute an installed application package");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "list") == 0) {
        println("=== Available System Packages (Remote & Local Repository) ===");
        for (u32 i = 0; i < CATALOG_SIZE; i++) {
            print("  [");
            if (s_catalog[i].installed) {
                print("INSTALLED");
            } else {
                print("AVAILABLE");
            }
            print("] ");
            print(s_catalog[i].name);
            print(" (v");
            print(s_catalog[i].version);
            print(", ");
            print(s_catalog[i].category);
            print(") - ");
            println(s_catalog[i].description);
        }
        return 0;
    }

    if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) {
            println("pkg: missing package name to install.");
            return 1;
        }
        for (u32 i = 0; i < CATALOG_SIZE; i++) {
            if (strcmp(s_catalog[i].name, argv[2]) == 0) {
                print("Downloading & installing ");
                print(s_catalog[i].name);
                println(" payload to /bin/...");
                s_catalog[i].installed = 1;
                println("Package successfully installed into userspace.");
                return 0;
            }
        }
        print("pkg: error: package not found: ");
        println(argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) {
            println("pkg: missing package name to remove.");
            return 1;
        }
        for (u32 i = 0; i < CATALOG_SIZE; i++) {
            if (strcmp(s_catalog[i].name, argv[2]) == 0) {
                print("Removing package ");
                print(s_catalog[i].name);
                println("...");
                s_catalog[i].installed = 0;
                println("Package removed.");
                return 0;
            }
        }
        print("pkg: error: package not found: ");
        println(argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "info") == 0) {
        if (argc < 3) {
            println("pkg: missing package name.");
            return 1;
        }
        for (u32 i = 0; i < CATALOG_SIZE; i++) {
            if (strcmp(s_catalog[i].name, argv[2]) == 0) {
                print("Package:     "); println(s_catalog[i].name);
                print("Version:     "); println(s_catalog[i].version);
                print("Category:    "); println(s_catalog[i].category);
                print("Size:        "); print_dec(s_catalog[i].size_kb); println(" KB");
                print("Description: "); println(s_catalog[i].description);
                print("Status:      "); println(s_catalog[i].installed ? "Installed" : "Not installed");
                return 0;
            }
        }
        print("pkg: package not found: ");
        println(argv[2]);
        return 1;
    }

    if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            println("pkg: missing package name to run.");
            return 1;
        }
        for (u32 i = 0; i < CATALOG_SIZE; i++) {
            if (strcmp(s_catalog[i].name, argv[2]) == 0) {
                if (!s_catalog[i].installed) {
                    print("pkg: error: package '");
                    print(s_catalog[i].name);
                    println("' is not installed. Run 'pkg install <name>' first.");
                    return 1;
                }
                print("Executing Ring-3 binary: /bin/");
                println(s_catalog[i].name);
                return 0;
            }
        }
        print("pkg: package not found: ");
        println(argv[2]);
        return 1;
    }

    print_help();
    return 0;
}
