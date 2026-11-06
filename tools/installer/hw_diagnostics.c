/* ==============================================================================
 * LastKernel Enterprise Server Edition — Hardware Diagnostics & Sysfs Scanner
 * Probes CPU Topology, NUMA Nodes, Memory Hierarchy, NVMe SMART & Block Devices
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

int installer_hw_probe_system(hw_system_summary_t* out_sys) {
    if (!out_sys) return -1;
    memset(out_sys, 0, sizeof(*out_sys));

    /* 1. Probe CPU Cores & Model from /proc/cpuinfo */
    out_sys->cpu_count = (u32)sysconf(_SC_NPROCESSORS_ONLN);
    if (out_sys->cpu_count == 0) out_sys->cpu_count = 1;

    FILE* f_cpu = fopen("/proc/cpuinfo", "r");
    if (f_cpu) {
        char line[256];
        while (fgets(line, sizeof(line), f_cpu)) {
            if (strncmp(line, "model name", 10) == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ') colon++;
                    size_t len = strlen(colon);
                    while (len > 0 && (colon[len - 1] == '\n' || colon[len - 1] == '\r')) {
                        colon[--len] = '\0';
                    }
                    strncpy(out_sys->cpu_model, colon, sizeof(out_sys->cpu_model) - 1);
                }
            }
            if (strncmp(line, "flags", 5) == 0) {
                if (strstr(line, " vmx ") || strstr(line, " svm ")) out_sys->has_vmx_hypervisor = 1;
                if (strstr(line, " pku ") || strstr(line, " ospke ")) out_sys->has_pkeys = 1;
                if (strstr(line, " aes ")) out_sys->has_aes_ni = 1;
                if (strstr(line, " avx512f ")) out_sys->has_avx512 = 1;
            }
        }
        fclose(f_cpu);
    }
    if (!out_sys->cpu_model[0]) {
        snprintf(out_sys->cpu_model, sizeof(out_sys->cpu_model), "x86_64 Enterprise Processor (%u Cores)", out_sys->cpu_count);
    }

    /* 2. Probe NUMA Nodes from /sys/devices/system/node/ */
    out_sys->numa_nodes_count = 1;
    DIR* node_dir = opendir("/sys/devices/system/node");
    if (node_dir) {
        struct dirent* de;
        u32 nodes = 0;
        while ((de = readdir(node_dir)) != NULL) {
            if (strncmp(de->d_name, "node", 4) == 0 && de->d_name[4] >= '0' && de->d_name[4] <= '9') {
                nodes++;
            }
        }
        if (nodes > 0) out_sys->numa_nodes_count = nodes;
        closedir(node_dir);
    }

    /* 3. Probe Physical RAM from /proc/meminfo */
    FILE* f_mem = fopen("/proc/meminfo", "r");
    if (f_mem) {
        char line[256];
        while (fgets(line, sizeof(line), f_mem)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                unsigned long long kb = 0;
                if (sscanf(line + 9, "%llu", &kb) == 1) {
                    out_sys->total_ram_mb = kb / 1024ULL;
                }
            }
            if (strncmp(line, "MemAvailable:", 13) == 0 || strncmp(line, "MemFree:", 8) == 0) {
                unsigned long long kb = 0;
                if (sscanf(line + (line[3] == 'A' ? 13 : 8), "%llu", &kb) == 1) {
                    out_sys->free_ram_mb = kb / 1024ULL;
                }
            }
        }
        fclose(f_mem);
    }
    if (out_sys->total_ram_mb == 0) out_sys->total_ram_mb = 16384; /* 16GB fallback */

    return 0;
}

int installer_hw_probe_disks(hw_disk_device_t* out_disks, u32 max_disks) {
    return installer_probe_disks(out_disks, max_disks);
}
