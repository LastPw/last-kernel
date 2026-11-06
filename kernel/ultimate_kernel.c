#include "ultimate_kernel.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "hal.h"
#include "core.h"
#include "sdc.h"
#include "ai_security.h"
#include "antivirus.h"
#include "ultimate.h"
#include "pcoe2.h"
#include "scheduler.h"
#include "power.h"
#include "timer.h"
#include "string.h"
#include "module.h"

#define UK_OVR_MODE     0x01u
#define UK_OVR_QUANTUM  0x02u
#define UK_OVR_RENDER   0x04u
#define UK_OVR_GHOST    0x08u
#define UK_OVR_PROFILE  0x10u
#define UK_OVR_POWER    0x20u

static ukernel_status_t st;
static u32 total_ram = 0;
static u32 override_mask = 0;

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static void preload_common(void) {
    module_load("resource");
    module_load("security");
    module_load("hal");
    module_load("ultimate");
}

void ukernel_preload_profile(ukernel_mode_t mode) {
    preload_common();
    if (mode == UKERNEL_MODE_SERVER || mode == UKERNEL_MODE_VPS) {
        module_load("firewall");
    } else {
        module_load("filetools");
    }
}

static void map_mode_to_stacks(ukernel_mode_t mode) {
    if ((override_mask & UK_OVR_PROFILE) == 0u) {
        if (mode == UKERNEL_MODE_SERVER) {
            resource_engine_set_profile(RE_PROFILE_SERVER_FULL);
        } else if (mode == UKERNEL_MODE_VPS) {
            resource_engine_set_profile(RE_PROFILE_SERVER_LIGHT);
        } else if (mode == UKERNEL_MODE_DESKTOP) {
            resource_engine_set_profile(RE_PROFILE_DESKTOP_FULL);
        } else {
            resource_engine_set_profile(RE_PROFILE_AUTO);
        }
    }

    if ((override_mask & UK_OVR_MODE) == 0u) {
        if (mode == UKERNEL_MODE_SERVER || mode == UKERNEL_MODE_VPS) {
            hal_set_mode(HAL_MODE_SERVER);
            sdc_set_mode(SDC_MODE_SERVER);
            ultimate_set_mode(mode == UKERNEL_MODE_VPS ? ULTIMATE_MODE_VPS : ULTIMATE_MODE_SERVER);
        } else if (mode == UKERNEL_MODE_DESKTOP) {
            hal_set_mode(HAL_MODE_DESKTOP);
            sdc_set_mode(SDC_MODE_DESKTOP);
            ultimate_set_mode(ULTIMATE_MODE_DESKTOP);
        } else {
            hal_set_mode(HAL_MODE_AUTO);
            ultimate_set_mode(ULTIMATE_MODE_AUTO);
        }
    }
}

void ukernel_init(const cpu_info_t* cpu, u32 ram_bytes, u32 cores) {
    (void)cpu;
    st.mode = UKERNEL_MODE_AUTO;
    st.perf_score = 0;
    st.risk_score = 0;
    st.thermal_score = 0;
    st.memory_pressure = 0;
    st.network_pressure = 0;
    st.latency_shadow_level = 1;
    st.scheduler_quantum_level = 2;
    st.sandbox_lockdown = 0;
    st.self_heal_actions = 0;
    st.owner_lock_open = 0;
    st.owner_lock_expires_tick = 0;
    st.handcrafted_overrides = 0;

    total_ram = ram_bytes;
    (void)cores;
    override_mask = 0;

    if (core_boot_mode() == BOOT_MODE_DESKTOP) st.mode = UKERNEL_MODE_DESKTOP;
    else if (core_boot_mode() == BOOT_MODE_SERVER) st.mode = UKERNEL_MODE_SERVER;
    else if (core_boot_mode() == BOOT_MODE_VPS) st.mode = UKERNEL_MODE_VPS;
    else st.mode = UKERNEL_MODE_AUTO;

    map_mode_to_stacks(st.mode);
    ukernel_preload_profile(st.mode);
}

void ukernel_tick(void) {
    resource_snapshot_t rs;
    resource_engine_snapshot_t re;
    pcoe2_status_t p2;
    sdc_status_t sdc_st;
    antivirus_status_t av;
    u32 mem_percent = 0;

    resource_monitor_snapshot(&rs);
    resource_engine_snapshot(&re);
    pcoe2_status(&p2);
    sdc_status(&sdc_st);
    antivirus_status(&av);

    if (rs.mem_total_bytes) {
        mem_percent = (rs.mem_used_bytes * 100u) / rs.mem_total_bytes;
    } else if (total_ram) {
        mem_percent = (rs.mem_used_bytes * 100u) / total_ram;
    }
    st.memory_pressure = mem_percent;
    st.network_pressure = clamp100((rs.net_packets / 4u) + (sdc_st.network_suspicious * 3u));
    st.thermal_score = clamp100(re.thermal_celsius);
    st.risk_score = p2.security_pressure;
    st.perf_score = p2.clroe_score;
    st.latency_shadow_level = p2.latency_shadow_level;

    if (st.mode == UKERNEL_MODE_AUTO && (override_mask & UK_OVR_MODE) == 0u) {
        if (rs.profile == RE_PROFILE_SERVER_FULL || rs.profile == RE_PROFILE_SERVER_LIGHT || sdc_st.mode == SDC_MODE_SERVER) {
            st.mode = UKERNEL_MODE_SERVER;
        } else {
            st.mode = UKERNEL_MODE_DESKTOP;
        }
    }

    if ((override_mask & UK_OVR_QUANTUM) == 0u) {
        st.scheduler_quantum_level = p2.scheduler_quantum_level;
        scheduler_set_quantum_level(st.scheduler_quantum_level);
    }

    map_mode_to_stacks(st.mode);

    if (rs.low_mem_warning || re.thermal_celsius > 90u || av.filesystem_integrity_percent < 95u) {
        st.self_heal_actions++;
    }

    st.sandbox_lockdown = (st.risk_score > 75u || av.active_threats > 0u) ? 1u : 0u;

    if (st.owner_lock_open && timer_ticks() > st.owner_lock_expires_tick) {
        st.owner_lock_open = 0;
        override_mask = 0;
    }
}

void ukernel_status(ukernel_status_t* out) {
    if (!out) return;
    *out = st;
}

void ukernel_set_mode(ukernel_mode_t mode) {
    if (mode <= UKERNEL_MODE_VPS) {
        st.mode = mode;
        map_mode_to_stacks(st.mode);
        ukernel_preload_profile(st.mode);
    }
}

ukernel_mode_t ukernel_get_mode(void) {
    return st.mode;
}

int ukernel_auth_owner(const char* user, const char* token) {
    if (!user || !token) {
        return -1;
    }
    if (str_cmp(user, "taha") != 0 &&
        str_cmp(user, "طاها") != 0 &&
        str_cmp(user, "Ø·Ø§Ù‡Ø§") != 0) {
        return -1;
    }
    if (str_cmp(token, "lastos-kernel-ultimate") != 0) {
        return -1;
    }
    st.owner_lock_open = 1;
    st.owner_lock_expires_tick = timer_ticks() + 5000u;
    return 0;
}

int ukernel_apply_override(const char* key, u32 value) {
    if (!key || !key[0]) return -1;
    if (!st.owner_lock_open || timer_ticks() > st.owner_lock_expires_tick) {
        st.owner_lock_open = 0;
        override_mask = 0;
        return -2;
    }

    if (str_cmp(key, "mode") == 0) {
        override_mask |= UK_OVR_MODE;
        ukernel_set_mode((value > 3u) ? UKERNEL_MODE_AUTO : (ukernel_mode_t)value);
    } else if (str_cmp(key, "quantum") == 0) {
        override_mask |= UK_OVR_QUANTUM;
        st.scheduler_quantum_level = (value < 1u) ? 1u : (value > 3u ? 3u : value);
        scheduler_set_quantum_level(st.scheduler_quantum_level);
    } else if (str_cmp(key, "render") == 0) {
        override_mask |= UK_OVR_RENDER;
        hal_set_render_policy(value == 0u ? HAL_RENDER_AUTO : (value == 1u ? HAL_RENDER_QUALITY : HAL_RENDER_PERFORMANCE));
    } else if (str_cmp(key, "ghost") == 0) {
        override_mask |= UK_OVR_GHOST;
        hal_set_ghost_ram(value ? 1u : 0u);
    } else if (str_cmp(key, "profile") == 0) {
        override_mask |= UK_OVR_PROFILE;
        if (value > 4u) value = 0u;
        resource_engine_set_profile((re_profile_t)value);
    } else if (str_cmp(key, "power") == 0) {
        override_mask |= UK_OVR_POWER;
        if (value > 3u) value = 0u;
        power_set_mode((power_mode_t)value);
    } else {
        return -3;
    }

    st.handcrafted_overrides++;
    return 0;
}

void ukernel_rollback_overrides(void) {
    override_mask = 0;
    st.handcrafted_overrides = 0;
    st.owner_lock_open = 0;
    st.mode = UKERNEL_MODE_AUTO;
    st.scheduler_quantum_level = 2;
    scheduler_set_quantum_level(st.scheduler_quantum_level);
}
