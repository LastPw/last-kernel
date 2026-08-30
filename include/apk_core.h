#ifndef APK_CORE_H
#define APK_CORE_H

#include "types.h"
#include "idt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APK_DOMAIN_CORE     0u
#define APK_DOMAIN_STORAGE  1u
#define APK_DOMAIN_NET      2u
#define APK_DOMAIN_GFX      3u
#define APK_DOMAIN_COMPAT   4u
#define APK_DOMAIN_USER     5u
#define APK_MAX_DOMAINS     6u

#define APK_CAP_DMA         (1u << 0)
#define APK_CAP_IO_PORT     (1u << 1)
#define APK_CAP_MMIO        (1u << 2)
#define APK_CAP_NET_RAW     (1u << 3)
#define APK_CAP_FS_RAW      (1u << 4)
#define APK_CAP_GPU_BLIT    (1u << 5)
#define APK_CAP_SYS_ADMIN   (1u << 6)

typedef struct {
    u32 domain_id;
    char name[32];
    u8 active;
    u8 quarantined;
    u32 pkey_tag;
    u32 capabilities;
    u32 fault_count;
    u32 heal_count;
    u64 memory_quota_bytes;
    u64 allocated_bytes;
    u64 peak_allocated_bytes;
    u32 active_threads;
    u32 last_checkpoint_tick;
    registers_t checkpoint_regs;
} apk_domain_desc_t;

typedef struct {
    u32 current_morph_mode;
    u32 auto_morph_enabled;
    u32 total_domains;
    u32 active_domains;
    u32 total_heals;
    u32 total_faults_isolated;
    u32 total_morph_switches;
    u32 avg_heal_latency_us;
    u32 pkey_enforced;
    u32 quarantined_domains;
    u64 total_tx_commits;
    u64 total_tx_rollbacks;
} apk_core_telemetry_t;

int  apk_core_init(void);
void apk_core_set_current_domain(u32 domain_id);
u32  apk_core_get_current_domain(void);
int  apk_core_has_capability(u32 domain_id, u32 cap);
int  apk_core_grant_capability(u32 domain_id, u32 cap);
int  apk_core_revoke_capability(u32 domain_id, u32 cap);

int  apk_core_set_memory_quota(u32 domain_id, u64 quota_bytes);
int  apk_core_track_alloc(u32 domain_id, u64 bytes);
int  apk_core_track_free(u32 domain_id, u64 bytes);

void apk_core_get_telemetry(apk_core_telemetry_t* out);
int  apk_core_get_domain(u32 domain_id, apk_domain_desc_t* out);

#ifdef __cplusplus
}
#endif

#endif /* APK_CORE_H */
