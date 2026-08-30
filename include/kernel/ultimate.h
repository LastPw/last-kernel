#ifndef ULTIMATE_H
#define ULTIMATE_H

#include "types.h"
#include "cpu_detect.h"

typedef enum {
    ULTIMATE_MODE_AUTO = 0,
    ULTIMATE_MODE_DESKTOP = 1,
    ULTIMATE_MODE_SERVER = 2,
    ULTIMATE_MODE_VPS = 3
} ultimate_mode_t;

typedef struct {
    ultimate_mode_t mode;
    u32 cpu_predict_score;
    u32 ram_predict_score;
    u32 gpu_predict_score;
    u32 storage_predict_score;
    u32 network_predict_score;
    u32 thermal_guard_level;
    u32 adaptive_quantum_level;
    u32 task_cluster_score;
    u32 resource_shadow_ready;
    u32 latency_shadow_level;
    u32 self_heal_actions;
    u32 instant_vm_switch_ready;
    u32 energy_awareness_active;
    u32 handcrafted_tweaks;
    u32 owner_lock_open;
    u32 owner_lock_expires_tick;
} ultimate_status_t;

void ultimate_init(u32 ram_bytes, u32 cores, const cpu_info_t* cpu);
void ultimate_tick(void);
void ultimate_status_get(ultimate_status_t* out);
void ultimate_set_mode(ultimate_mode_t mode);
ultimate_mode_t ultimate_get_mode(void);
int ultimate_authorize_owner(const char* user, const char* key);
int ultimate_apply_tweak(const char* key, u32 value);
void ultimate_rollback_tweaks(void);

#endif
