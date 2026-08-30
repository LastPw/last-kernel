#ifndef ULTIMATE_KERNEL_H
#define ULTIMATE_KERNEL_H

#include "types.h"
#include "bootinfo.h"
#include "cpu_detect.h"

typedef enum {
    UKERNEL_MODE_AUTO = 0,
    UKERNEL_MODE_DESKTOP = 1,
    UKERNEL_MODE_SERVER = 2,
    UKERNEL_MODE_VPS = 3
} ukernel_mode_t;

typedef struct {
    ukernel_mode_t mode;
    u32 perf_score;
    u32 risk_score;
    u32 thermal_score;
    u32 memory_pressure;
    u32 network_pressure;
    u32 latency_shadow_level;
    u32 scheduler_quantum_level;
    u32 sandbox_lockdown;
    u32 self_heal_actions;
    u32 owner_lock_open;
    u32 owner_lock_expires_tick;
    u32 handcrafted_overrides;
} ukernel_status_t;

void ukernel_init(const cpu_info_t* cpu, u32 ram_bytes, u32 cores);
void ukernel_tick(void);
void ukernel_status(ukernel_status_t* out);
void ukernel_set_mode(ukernel_mode_t mode);
ukernel_mode_t ukernel_get_mode(void);
int ukernel_auth_owner(const char* user, const char* token);
int ukernel_apply_override(const char* key, u32 value);
void ukernel_rollback_overrides(void);
void ukernel_preload_profile(ukernel_mode_t mode);

#endif
