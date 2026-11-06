#include "ultimate.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "ai_security.h"
#include "sdc.h"
#include "hal.h"
#include "power.h"
#include "timer.h"
#include "string.h"
#include "scheduler.h"

typedef struct {
    char key[20];
    u32 value;
    u8 used;
} tweak_t;

#define ULT_TWEAK_MAX 16

static ultimate_status_t st;
static tweak_t tweaks[ULT_TWEAK_MAX];
static u32 total_ram = 0;
static u32 total_cores = 1;
static u32 last_net_packets = 0;
static u32 security_weight = 50;

static u32 clamp100(u32 v) {
    if (v > 100u) {
        return 100u;
    }
    return v;
}

static void apply_policy_desktop(u32 pressure) {
    if (pressure > 78u) {
        power_set_mode(POWER_MODE_BALANCED);
        hal_set_render_policy(HAL_RENDER_PERFORMANCE);
        st.energy_awareness_active = 1;
    } else {
        power_set_mode(POWER_MODE_BALANCED);
        hal_set_render_policy(HAL_RENDER_QUALITY);
        st.energy_awareness_active = 0;
    }
}

static void apply_policy_server(u32 pressure) {
    if (pressure > 86u || st.thermal_guard_level > 2u) {
        power_set_mode(POWER_MODE_BALANCED);
    } else {
        power_set_mode(POWER_MODE_PERFORMANCE);
    }
    hal_set_render_policy(HAL_RENDER_PERFORMANCE);
    st.energy_awareness_active = 0;
}

static void apply_policy_vps(u32 pressure) {
    if (pressure > 84u || st.thermal_guard_level > 2u) {
        power_set_mode(POWER_MODE_LOW_POWER);
        st.energy_awareness_active = 1;
    } else {
        power_set_mode(POWER_MODE_BALANCED);
        st.energy_awareness_active = 0;
    }
    hal_set_render_policy(HAL_RENDER_PERFORMANCE);
}

static void refresh_mode_from_context(const resource_snapshot_t* rs, const sdc_status_t* sdc_st) {
    if (st.mode != ULTIMATE_MODE_AUTO) {
        return;
    }
    if (!rs || !sdc_st) {
        return;
    }
    if (rs->profile == RE_PROFILE_SERVER_FULL || rs->profile == RE_PROFILE_SERVER_LIGHT || sdc_st->mode == SDC_MODE_SERVER) {
        st.mode = ULTIMATE_MODE_SERVER;
    } else {
        st.mode = ULTIMATE_MODE_DESKTOP;
    }
}

void ultimate_init(u32 ram_bytes, u32 cores, const cpu_info_t* cpu) {
    u32 i;
    (void)cpu;

    st.mode = ULTIMATE_MODE_AUTO;
    st.cpu_predict_score = 0;
    st.ram_predict_score = 0;
    st.gpu_predict_score = 0;
    st.storage_predict_score = 0;
    st.network_predict_score = 0;
    st.thermal_guard_level = 1;
    st.adaptive_quantum_level = 2;
    st.task_cluster_score = 0;
    st.resource_shadow_ready = 0;
    st.latency_shadow_level = 1;
    st.self_heal_actions = 0;
    st.instant_vm_switch_ready = 0;
    st.energy_awareness_active = 0;
    st.handcrafted_tweaks = 0;
    st.owner_lock_open = 0;
    st.owner_lock_expires_tick = 0;

    total_ram = ram_bytes;
    total_cores = cores ? cores : 1;
    last_net_packets = 0;
    security_weight = 50;

    for (i = 0; i < ULT_TWEAK_MAX; i++) {
        tweaks[i].used = 0;
        tweaks[i].key[0] = '\0';
        tweaks[i].value = 0;
    }
}

void ultimate_tick(void) {
    resource_snapshot_t rs;
    resource_engine_snapshot_t re;
    ai_security_status_t ai;
    sdc_status_t sdc_st;
    hal_status_t hal_st;
    u32 net_delta = 0;
    u32 mem_percent = 0;
    u32 pressure = 0;

    resource_monitor_snapshot(&rs);
    resource_engine_snapshot(&re);
    ai_security_status(&ai);
    sdc_status(&sdc_st);
    hal_status(&hal_st);

    if (rs.mem_total_bytes) {
        mem_percent = (rs.mem_used_bytes * 100u) / rs.mem_total_bytes;
    }

    if (rs.net_packets >= last_net_packets) {
        net_delta = rs.net_packets - last_net_packets;
    } else {
        net_delta = rs.net_packets;
    }
    last_net_packets = rs.net_packets;

    st.cpu_predict_score = clamp100((rs.cpu_busy_percent * 3u + ai.risk_score / 3u + hal_st.hetero_core_score) / 5u);
    st.ram_predict_score = clamp100((mem_percent * 3u + (rs.predicted_next_alloc / 131072u)) / 4u);
    st.gpu_predict_score = clamp100((hal_st.smart_render_level * 25u) + (ai.predicted_threat_window / 8u));
    st.storage_predict_score = clamp100((rs.io_ops_total % 100u) + (hal_st.data_prefetch_score / 2u));
    st.network_predict_score = clamp100((net_delta * 2u) + (sdc_st.network_suspicious * 4u));

    st.thermal_guard_level = re.thermal_celsius > 84u ? 3u : (re.thermal_celsius > 74u ? 2u : 1u);
    st.task_cluster_score = clamp100((total_cores * 8u) + (hal_st.predicted_threads * 6u) + (rs.cpu_busy_percent / 2u));
    st.adaptive_quantum_level = (st.cpu_predict_score > 80u || st.network_predict_score > 80u) ? 1u : (st.cpu_predict_score > 45u ? 2u : 3u);
    st.latency_shadow_level = (st.network_predict_score > 70u) ? 3u : (st.network_predict_score > 35u ? 2u : 1u);

    st.resource_shadow_ready = (st.cpu_predict_score > 65u || st.ram_predict_score > 65u || st.network_predict_score > 65u) ? 1u : 0u;
    st.instant_vm_switch_ready = (hal_st.ghost_ram_enabled && hal_st.ghost_ram_reserved_kb > 4096u) ? 1u : 0u;

    refresh_mode_from_context(&rs, &sdc_st);

    pressure = (st.cpu_predict_score + st.ram_predict_score + st.network_predict_score + (security_weight / 2u)) / 4u;

    if (rs.low_mem_warning || st.thermal_guard_level == 3u) {
        st.self_heal_actions++;
    }

    if (st.mode == ULTIMATE_MODE_DESKTOP) {
        apply_policy_desktop(pressure);
    } else if (st.mode == ULTIMATE_MODE_SERVER) {
        apply_policy_server(pressure);
    } else if (st.mode == ULTIMATE_MODE_VPS) {
        apply_policy_vps(pressure);
    } else {
        apply_policy_desktop(pressure);
    }
    scheduler_set_quantum_level(st.adaptive_quantum_level);

    if (st.owner_lock_open && timer_ticks() > st.owner_lock_expires_tick) {
        st.owner_lock_open = 0;
    }
}

void ultimate_status_get(ultimate_status_t* out) {
    if (!out) {
        return;
    }
    *out = st;
}

void ultimate_set_mode(ultimate_mode_t mode) {
    if (mode == ULTIMATE_MODE_AUTO || mode == ULTIMATE_MODE_DESKTOP || mode == ULTIMATE_MODE_SERVER || mode == ULTIMATE_MODE_VPS) {
        st.mode = mode;
    }
}

ultimate_mode_t ultimate_get_mode(void) {
    return st.mode;
}

int ultimate_authorize_owner(const char* user, const char* key) {
    /*
     * Handcrafted optimization lock:
     * - Owner identity is mapped to "taha" in this stage.
     * - Integration layer can map localized names (e.g. Persian) to this identity.
     */
    if (!user || !key) {
        return -1;
    }
    if (str_cmp(user, "taha") != 0 && str_cmp(user, "طاها") != 0) {
        return -1;
    }
    if (str_cmp(key, "lastos-ultimate") != 0) {
        return -1;
    }
    st.owner_lock_open = 1;
    st.owner_lock_expires_tick = timer_ticks() + 3000u;
    return 0;
}

int ultimate_apply_tweak(const char* key, u32 value) {
    u32 i;
    if (!key || !key[0]) {
        return -1;
    }
    if (!st.owner_lock_open || timer_ticks() > st.owner_lock_expires_tick) {
        st.owner_lock_open = 0;
        return -2;
    }

    for (i = 0; i < ULT_TWEAK_MAX; i++) {
        if (!tweaks[i].used || str_cmp(tweaks[i].key, key) == 0) {
            tweaks[i].used = 1;
            str_ncpy(tweaks[i].key, key, sizeof(tweaks[i].key));
            tweaks[i].value = value;
            break;
        }
    }
    if (i >= ULT_TWEAK_MAX) {
        return -3;
    }

    if (str_cmp(key, "security_weight") == 0) {
        security_weight = value > 100u ? 100u : value;
    } else if (str_cmp(key, "quantum") == 0) {
        st.adaptive_quantum_level = (value > 3u) ? 3u : value;
        scheduler_set_quantum_level(st.adaptive_quantum_level);
    } else if (str_cmp(key, "thermal_guard") == 0) {
        st.thermal_guard_level = (value > 3u) ? 3u : value;
    } else if (str_cmp(key, "ghost_boost") == 0) {
        hal_set_ghost_ram(value ? 1u : 0u);
    } else if (str_cmp(key, "render") == 0) {
        hal_set_render_policy(value == 0u ? HAL_RENDER_AUTO : (value == 1u ? HAL_RENDER_QUALITY : HAL_RENDER_PERFORMANCE));
    } else if (str_cmp(key, "mode") == 0) {
        ultimate_set_mode((value > 3u) ? ULTIMATE_MODE_AUTO : (ultimate_mode_t)value);
    }

    st.handcrafted_tweaks++;
    return 0;
}

void ultimate_rollback_tweaks(void) {
    u32 i;
    for (i = 0; i < ULT_TWEAK_MAX; i++) {
        tweaks[i].used = 0;
        tweaks[i].key[0] = '\0';
        tweaks[i].value = 0;
    }
    security_weight = 50;
    st.handcrafted_tweaks = 0;
    st.owner_lock_open = 0;
    st.mode = ULTIMATE_MODE_AUTO;
    st.adaptive_quantum_level = 2;
    scheduler_set_quantum_level(st.adaptive_quantum_level);
    hal_set_render_policy(HAL_RENDER_AUTO);
}
