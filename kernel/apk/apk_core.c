#include "apk_core.h"
#include "string.h"
#include "serial.h"
#include "timer.h"
#include "spinlock.h"
#include "paging.h"

/*
 * APK Core Control Plane Subsystem
 *
 * Manages the Ring-0 Zero-Context Micro-Domain matrix,
 * hardware capabilities, memory quotas, and thread isolation.
 */

static apk_domain_desc_t s_domains[APK_MAX_DOMAINS];
static apk_core_telemetry_t s_telemetry;
static spinlock_t s_core_lock;
static u8 s_core_inited = 0;
static u32 s_active_domain = APK_DOMAIN_CORE;

int apk_core_init(void) {
    if (s_core_inited) return 0;
    spinlock_init(&s_core_lock);

    memset(s_domains, 0, sizeof(s_domains));
    memset(&s_telemetry, 0, sizeof(s_telemetry));

    /* Domain 0: Kernel Core (All Capabilities, Uncapped) */
    s_domains[APK_DOMAIN_CORE].domain_id = APK_DOMAIN_CORE;
    str_cpy(s_domains[APK_DOMAIN_CORE].name, "Kernel Core");
    s_domains[APK_DOMAIN_CORE].active = 1;
    s_domains[APK_DOMAIN_CORE].pkey_tag = 0;
    s_domains[APK_DOMAIN_CORE].capabilities = 0xFFFFFFFFu;
    s_domains[APK_DOMAIN_CORE].memory_quota_bytes = 0xFFFFFFFFFFFFFFFFull;

    /* Domain 1: Storage/VFS Subsystem (DMA + MMIO + FS_RAW, 256MB) */
    s_domains[APK_DOMAIN_STORAGE].domain_id = APK_DOMAIN_STORAGE;
    str_cpy(s_domains[APK_DOMAIN_STORAGE].name, "Storage/VFS Subsystem");
    s_domains[APK_DOMAIN_STORAGE].active = 1;
    s_domains[APK_DOMAIN_STORAGE].pkey_tag = 1;
    s_domains[APK_DOMAIN_STORAGE].capabilities = APK_CAP_DMA | APK_CAP_IO_PORT | APK_CAP_MMIO | APK_CAP_FS_RAW;
    s_domains[APK_DOMAIN_STORAGE].memory_quota_bytes = 256ull * 1024ull * 1024ull;

    /* Domain 2: Network Subsystem (DMA + MMIO + NET_RAW, 128MB) */
    s_domains[APK_DOMAIN_NET].domain_id = APK_DOMAIN_NET;
    str_cpy(s_domains[APK_DOMAIN_NET].name, "Network Protocol Stack");
    s_domains[APK_DOMAIN_NET].active = 1;
    s_domains[APK_DOMAIN_NET].pkey_tag = 2;
    s_domains[APK_DOMAIN_NET].capabilities = APK_CAP_DMA | APK_CAP_IO_PORT | APK_CAP_MMIO | APK_CAP_NET_RAW;
    s_domains[APK_DOMAIN_NET].memory_quota_bytes = 128ull * 1024ull * 1024ull;

    /* Domain 3: Graphics & GUI Compositor (MMIO + GPU_BLIT, 512MB) */
    s_domains[APK_DOMAIN_GFX].domain_id = APK_DOMAIN_GFX;
    str_cpy(s_domains[APK_DOMAIN_GFX].name, "Graphics & Compositor");
    s_domains[APK_DOMAIN_GFX].active = 1;
    s_domains[APK_DOMAIN_GFX].pkey_tag = 3;
    s_domains[APK_DOMAIN_GFX].capabilities = APK_CAP_MMIO | APK_CAP_GPU_BLIT;
    s_domains[APK_DOMAIN_GFX].memory_quota_bytes = 512ull * 1024ull * 1024ull;

    /* Domain 4: Compatibility Subsystems (Linuxulator/POSIX/WinNT, 256MB) */
    s_domains[APK_DOMAIN_COMPAT].domain_id = APK_DOMAIN_COMPAT;
    str_cpy(s_domains[APK_DOMAIN_COMPAT].name, "Multi-OS Compat Engine");
    s_domains[APK_DOMAIN_COMPAT].active = 1;
    s_domains[APK_DOMAIN_COMPAT].pkey_tag = 4;
    s_domains[APK_DOMAIN_COMPAT].capabilities = APK_CAP_FS_RAW | APK_CAP_NET_RAW | APK_CAP_GPU_BLIT;
    s_domains[APK_DOMAIN_COMPAT].memory_quota_bytes = 256ull * 1024ull * 1024ull;

    /* Domain 5: User Applications Sandboxes (Ring 3, 2048MB) */
    s_domains[APK_DOMAIN_USER].domain_id = APK_DOMAIN_USER;
    str_cpy(s_domains[APK_DOMAIN_USER].name, "User Ring-3 Sandboxes");
    s_domains[APK_DOMAIN_USER].active = 1;
    s_domains[APK_DOMAIN_USER].pkey_tag = 5;
    s_domains[APK_DOMAIN_USER].capabilities = 0; /* Pure restricted sandbox */
    s_domains[APK_DOMAIN_USER].memory_quota_bytes = 2048ull * 1024ull * 1024ull;

    s_telemetry.current_morph_mode = 0; /* BALANCED */
    s_telemetry.auto_morph_enabled = 1;
    s_telemetry.total_domains = APK_MAX_DOMAINS;
    s_telemetry.active_domains = APK_MAX_DOMAINS;
    s_telemetry.avg_heal_latency_us = 4;
    s_telemetry.pkey_enforced = paging_pku_supported();

    s_core_inited = 1;
    serial_write("[APK_CORE] Initialized Micro-Domain Control Matrix (PKEY & Hardware Isolation)\n");
    return 0;
}

void apk_core_set_current_domain(u32 domain_id) {
    if (domain_id < APK_MAX_DOMAINS) {
        s_active_domain = domain_id;
        /* Enforce PKEY hardware access permissions if supported */
        if (s_telemetry.pkey_enforced && domain_id != APK_DOMAIN_CORE) {
            paging_set_domain_rights(domain_id, 0, 0);
        }
    }
}

u32 apk_core_get_current_domain(void) {
    return s_active_domain;
}

int apk_core_has_capability(u32 domain_id, u32 cap) {
    if (domain_id >= APK_MAX_DOMAINS) return 0;
    return (s_domains[domain_id].capabilities & cap) == cap;
}

int apk_core_grant_capability(u32 domain_id, u32 cap) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    spinlock_lock(&s_core_lock);
    s_domains[domain_id].capabilities |= cap;
    spinlock_unlock(&s_core_lock);
    return 0;
}

int apk_core_revoke_capability(u32 domain_id, u32 cap) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    spinlock_lock(&s_core_lock);
    s_domains[domain_id].capabilities &= ~cap;
    spinlock_unlock(&s_core_lock);
    return 0;
}

int apk_core_set_memory_quota(u32 domain_id, u64 quota_bytes) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    spinlock_lock(&s_core_lock);
    s_domains[domain_id].memory_quota_bytes = quota_bytes;
    spinlock_unlock(&s_core_lock);
    return 0;
}

int apk_core_track_alloc(u32 domain_id, u64 bytes) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    spinlock_lock(&s_core_lock);
    if (s_domains[domain_id].allocated_bytes + bytes > s_domains[domain_id].memory_quota_bytes) {
        spinlock_unlock(&s_core_lock);
        return -1; /* Quota exceeded */
    }
    s_domains[domain_id].allocated_bytes += bytes;
    if (s_domains[domain_id].allocated_bytes > s_domains[domain_id].peak_allocated_bytes) {
        s_domains[domain_id].peak_allocated_bytes = s_domains[domain_id].allocated_bytes;
    }
    spinlock_unlock(&s_core_lock);
    return 0;
}

int apk_core_track_free(u32 domain_id, u64 bytes) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    spinlock_lock(&s_core_lock);
    if (s_domains[domain_id].allocated_bytes >= bytes) {
        s_domains[domain_id].allocated_bytes -= bytes;
    } else {
        s_domains[domain_id].allocated_bytes = 0;
    }
    spinlock_unlock(&s_core_lock);
    return 0;
}

void apk_core_get_telemetry(apk_core_telemetry_t* out) {
    if (!out) return;
    if (!s_core_inited) apk_core_init();
    spinlock_lock(&s_core_lock);
    *out = s_telemetry;
    spinlock_unlock(&s_core_lock);
}

int apk_core_get_domain(u32 domain_id, apk_domain_desc_t* out) {
    if (domain_id >= APK_MAX_DOMAINS || !out) return -1;
    if (!s_core_inited) apk_core_init();
    spinlock_lock(&s_core_lock);
    *out = s_domains[domain_id];
    spinlock_unlock(&s_core_lock);
    return 0;
}
