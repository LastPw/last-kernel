#include "hal.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "ai_security.h"
#include "sdc.h"
#include "module.h"
#include "driver_bus.h"
#include "threat_logger.h"
#include "string.h"

static hal_status_t st;
static u32 total_ram_bytes = 0;
static u32 core_count = 1;
static u32 last_net_packets = 0;
static u32 last_tick_threat_count = 0;

void hal_init(u32 ram_bytes, u32 cores, const cpu_info_t* cpu) {
    st.hetero_core_score = 0;
    st.predicted_threads = 1;
    st.thermal_guard_level = 1;
    st.legacy_tuning_active = 0;
    st.ghost_ram_enabled = 1;
    st.ghost_ram_reserved_kb = 0;
    st.smart_render_level = 2;
    st.gpu_accel_level = 1;
    st.data_prefetch_score = 0;
    st.bandwidth_optimizer_level = 1;
    st.memory_self_heal_events = 0;
    st.pnp_sandboxed_drivers = 0;
    st.driver_avg_health = 100;
    st.driver_recovery_actions = 0;
    st.advisor_confidence = 50;
    st.mode = HAL_MODE_AUTO;
    st.render_policy = HAL_RENDER_AUTO;

    total_ram_bytes = ram_bytes;
    core_count = cores ? cores : 1;
    last_net_packets = 0;
    last_tick_threat_count = threat_logger_count();

    if (cpu) {
        u32 perf_score = 0;
        if (cpu->has_avx) perf_score += 30;
        if (cpu->has_sse) perf_score += 15;
        if (cpu->has_apic) perf_score += 10;
        if (cpu->logical_cores > cpu->physical_cores) perf_score += 10;
        if (cpu->logical_cores > 4) perf_score += 20;
        st.hetero_core_score = perf_score;

        if (!cpu->is_modern_profile || !cpu->has_sse) {
            st.legacy_tuning_active = 1;
        }
    }
}

static void refresh_driver_sandbox_count(void) {
    u32 i;
    u32 n = module_count();
    u32 c = 0;
    for (i = 0; i < n; i++) {
        module_info_t mi;
        if (module_get_by_index(i, &mi) != 0) {
            continue;
        }
        if (mi.loaded && mi.sandboxed) {
            c++;
        }
    }
    st.pnp_sandboxed_drivers = c;
}

void hal_tick(void) {
    resource_snapshot_t rs;
    resource_engine_snapshot_t re;
    ai_security_status_t ai;
    sdc_status_t s;
    u32 cur_threats = threat_logger_count();
    u32 delta_threats = 0;
    u32 net_delta = 0;
    u32 reserve = 0;

    resource_monitor_snapshot(&rs);
    resource_engine_snapshot(&re);
    ai_security_status(&ai);
    sdc_status(&s);

    if (cur_threats >= last_tick_threat_count) {
        delta_threats = cur_threats - last_tick_threat_count;
    } else {
        delta_threats = cur_threats;
    }
    last_tick_threat_count = cur_threats;

    if (rs.net_packets >= last_net_packets) {
        net_delta = rs.net_packets - last_net_packets;
    } else {
        net_delta = rs.net_packets;
    }
    last_net_packets = rs.net_packets;

    st.predicted_threads = re.recommended_threads;
    if (st.predicted_threads == 0) {
        st.predicted_threads = 1;
    }
    if (st.predicted_threads > core_count) {
        st.predicted_threads = core_count;
    }

    if (re.thermal_celsius > 82 || ai.risk_score > 250) {
        st.thermal_guard_level = 3;
    } else if (re.thermal_celsius > 72 || ai.risk_score > 120) {
        st.thermal_guard_level = 2;
    } else {
        st.thermal_guard_level = 1;
    }

    if (st.ghost_ram_enabled) {
        reserve = total_ram_bytes / ((st.mode == HAL_MODE_SERVER) ? 40u : 24u);
        if (rs.low_mem_warning) {
            reserve /= 2u;
        }
        if (reserve > (16u * 1024u * 1024u)) {
            reserve = 16u * 1024u * 1024u;
        }
        st.ghost_ram_reserved_kb = reserve / 1024u;
    } else {
        st.ghost_ram_reserved_kb = 0;
    }

    if (st.render_policy == HAL_RENDER_QUALITY) {
        st.smart_render_level = 3;
    } else if (st.render_policy == HAL_RENDER_PERFORMANCE) {
        st.smart_render_level = 1;
    } else {
        if (rs.cpu_busy_percent > 85 || rs.low_mem_warning || ai.risk_score > 220) {
            st.smart_render_level = 1;
        } else if (rs.cpu_busy_percent > 60 || re.thermal_celsius > 74) {
            st.smart_render_level = 2;
        } else {
            st.smart_render_level = 3;
        }
    }

    if (st.mode == HAL_MODE_SERVER || net_delta > 120u || s.network_suspicious > 5u) {
        st.bandwidth_optimizer_level = 3;
    } else if (net_delta > 40u || s.network_suspicious > 0u) {
        st.bandwidth_optimizer_level = 2;
    } else {
        st.bandwidth_optimizer_level = 1;
    }

    st.data_prefetch_score = re.predicted_next_alloc / 4096u;
    if (st.data_prefetch_score > 100u) {
        st.data_prefetch_score = 100u;
    }

    st.gpu_accel_level = st.smart_render_level;
    if (st.mode == HAL_MODE_SERVER && st.gpu_accel_level > 2) {
        st.gpu_accel_level = 2;
    }

    st.memory_self_heal_events += delta_threats;
    if (st.memory_self_heal_events > 1000000u) {
        st.memory_self_heal_events = 1000000u;
    }

    if (st.mode == HAL_MODE_AUTO) {
        if (rs.profile == RE_PROFILE_SERVER_FULL || rs.profile == RE_PROFILE_SERVER_LIGHT || s.mode == SDC_MODE_SERVER) {
            st.mode = HAL_MODE_SERVER;
        } else {
            st.mode = HAL_MODE_DESKTOP;
        }
    }

    st.advisor_confidence = 50u + (st.hetero_core_score / 2u);
    if (ai.risk_score > 180) {
        if (st.advisor_confidence > 15u) {
            st.advisor_confidence -= 15u;
        }
    }
    if (st.advisor_confidence > 99u) {
        st.advisor_confidence = 99u;
    }

    refresh_driver_sandbox_count();

    {
        driver_bus_stats_t dbs;
        driver_bus_stats(&dbs);
        st.driver_avg_health = dbs.avg_health;
        if (dbs.avg_health < 55u && st.mode == HAL_MODE_SERVER) {
            for (u32 i = 0; i < driver_count(); i++) {
                driver_info_t d;
                if (driver_info_by_index(i, &d) != 0) continue;
                if (!d.active || d.health_score >= 45u) continue;
                if (driver_recover(d.name) == 0) {
                    st.driver_recovery_actions++;
                }
            }
        }
    }
}

void hal_status(hal_status_t* out) {
    if (!out) {
        return;
    }
    *out = st;
}

void hal_set_mode(hal_mode_t mode) {
    if (mode == HAL_MODE_AUTO || mode == HAL_MODE_DESKTOP || mode == HAL_MODE_SERVER) {
        st.mode = mode;
    }
}

hal_mode_t hal_get_mode(void) {
    return st.mode;
}

void hal_set_ghost_ram(u8 enabled) {
    st.ghost_ram_enabled = enabled ? 1u : 0u;
}

void hal_set_render_policy(hal_render_policy_t policy) {
    if (policy == HAL_RENDER_AUTO || policy == HAL_RENDER_QUALITY || policy == HAL_RENDER_PERFORMANCE) {
        st.render_policy = policy;
    }
}

int hal_driver_advice(const char* module_name, hal_driver_advice_t* out) {
    module_info_t m;
    if (!out || !module_name || !module_name[0]) {
        return -1;
    }
    if (module_get(module_name, &m) != 0) {
        return -1;
    }

    out->confidence = st.advisor_confidence;
    out->recommended_action = 0;

    if (!m.loaded && m.installed) {
        out->recommended_action = 1; /* load */
    } else if (m.loaded && !m.sandboxed) {
        out->recommended_action = 2; /* sandbox/harden */
    } else if (m.loaded && m.load_count > 1000u) {
        out->recommended_action = 3; /* patch update */
    }
    return 0;
}
