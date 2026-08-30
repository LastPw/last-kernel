#include "pcoe2.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "hal.h"
#include "sdc.h"
#include "ai_security.h"
#include "antivirus.h"
#include "scheduler.h"
#include "power.h"
#include "ultimate.h"
#include "module.h"
#include "timer.h"
#include "string.h"
#include "pcoe2_core.h"
#include "pcoe2_matrix.h"
#include "hyper_ghost.h"
#include "quantum_scheduler.h"

#define PCOE_OVR_MODE     0x01u
#define PCOE_OVR_QUANTUM  0x02u
#define PCOE_OVR_RENDER   0x04u
#define PCOE_OVR_GHOST    0x08u
#define PCOE_OVR_PROFILE  0x10u
#define PCOE_OVR_POWER    0x20u

static pcoe2_status_t st;
static u32 total_ram = 0;
static u32 total_cores = 1;
static u32 override_mask = 0;

/* Learning bias from handcrafted overrides (idea #10). */
static u32 bias_quantum = 0;
static u32 bias_power = 0;
static u32 bias_ghost = 0;
static u32 bias_render = 0;

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static void preload_common(void) {
    module_load("resource");
    module_load("security");
    module_load("hal");
    module_load("ultimate");
    module_load("pcoe2");
}

void pcoe2_preload_predictive(pcoe_mode_t mode) {
    preload_common();
    if (mode == PCOE_MODE_SERVER || mode == PCOE_MODE_VPS) {
        module_load("firewall");
        st.preload_level = 3;
    } else if (mode == PCOE_MODE_DESKTOP) {
        module_load("filetools");
        st.preload_level = 2;
    } else {
        st.preload_level = 1;
    }
}

static void apply_stack_policy(void) {
    if ((override_mask & PCOE_OVR_PROFILE) == 0u) {
        if (st.mode == PCOE_MODE_SERVER) {
            resource_engine_set_profile(RE_PROFILE_SERVER_FULL);
        } else if (st.mode == PCOE_MODE_VPS) {
            resource_engine_set_profile(RE_PROFILE_SERVER_LIGHT);
        } else if (st.mode == PCOE_MODE_DESKTOP) {
            resource_engine_set_profile(RE_PROFILE_DESKTOP_FULL);
        } else {
            resource_engine_set_profile(RE_PROFILE_AUTO);
        }
    }

    if ((override_mask & PCOE_OVR_MODE) == 0u) {
        if (st.mode == PCOE_MODE_SERVER || st.mode == PCOE_MODE_VPS) {
            hal_set_mode(HAL_MODE_SERVER);
            sdc_set_mode(SDC_MODE_SERVER);
            ultimate_set_mode(st.mode == PCOE_MODE_VPS ? ULTIMATE_MODE_VPS : ULTIMATE_MODE_SERVER);
        } else if (st.mode == PCOE_MODE_DESKTOP) {
            hal_set_mode(HAL_MODE_DESKTOP);
            sdc_set_mode(SDC_MODE_DESKTOP);
            ultimate_set_mode(ULTIMATE_MODE_DESKTOP);
        } else {
            hal_set_mode(HAL_MODE_AUTO);
            ultimate_set_mode(ULTIMATE_MODE_AUTO);
        }
    }
}

void pcoe2_init(u32 ram_bytes, u32 cores, const cpu_info_t* cpu) {
    st.mode = PCOE_MODE_AUTO;
    st.prs_score = 0;
    st.dmca_score = 0;
    st.ghost_score = 0;
    st.ai_sched_score = 0;
    st.clroe_score = 0;
    st.security_sandbox_score = 0;
    st.hybrid_mode_score = 0;
    st.ghost_gpu_score = 0;
    st.energy_thermal_score = 0;
    st.handcrafted_learning_score = 0;
    st.quantum_predictive_score = 0;
    st.core_anomaly_score = 0;
    st.core_stability_score = 100;
    st.core_predicted_pressure = 0;
    st.hyper_ghost_pressure = 0;
    st.hyper_ghost_latency_shadow = 1;
    st.hyper_ghost_reserved_kb = 0;
    st.hyper_ghost_hot_pool_kb = 0;
    st.hyper_ghost_warm_pool_kb = 0;
    st.hyper_ghost_cold_pool_kb = 0;
    st.hyper_ghost_shadow_pages = 0;
    st.hyper_ghost_compression_permille = 1000;
    st.hyper_ghost_dedup_permille = 1000;
    st.hyper_ghost_reclaim_events = 0;
    st.hyper_ghost_rebalance_events = 0;
    st.quantum_score = 0;
    st.quantum_pressure = 0;
    st.quantum_anomaly = 0;
    st.quantum_stability = 100;
    st.quantum_preempt_hint = 1;
    st.quantum_throughput_gain = 0;
    st.quantum_fairness_index = 100;
    st.quantum_predicted_threads = 1;
    st.quantum_dependency_depth = 1;
    st.quantum_clone_level = 0;
    st.quantum_micro_thread_level = 1;
    st.latency_shadow_level = 1;
    st.predicted_threads = 1;
    st.scheduler_quantum_level = 2;
    st.predicted_dependency_depth = 1;
    st.adaptive_clone_level = 0;
    st.micro_thread_level = 1;
    st.preload_level = 0;
    st.self_heal_level = 0;
    st.security_pressure = 0;
    st.owner_lock_open = 0;
    st.owner_lock_expires_tick = 0;
    st.handcrafted_overrides = 0;

    total_ram = ram_bytes;
    total_cores = cores ? cores : 1;
    override_mask = 0;
    bias_quantum = 0;
    bias_power = 0;
    bias_ghost = 0;
    bias_render = 0;
    pcoe2_core_init();
    pcoe2_matrix_init(ram_bytes, total_cores);
    hyper_ghost_init(ram_bytes);
    quantum_scheduler_init(total_cores);

    if (cpu && cpu->logical_cores >= 4u) {
        st.dmca_score = 60u;
    }
    pcoe2_preload_predictive(st.mode);
}

void pcoe2_tick(void) {
    resource_snapshot_t rs;
    resource_engine_snapshot_t re;
    hal_status_t hs;
    sdc_status_t sdc_st;
    ai_security_status_t ai;
    antivirus_status_t av;
    u32 mem_percent = 0;
    u32 io_pressure = 0;
    u32 prs = 0;
    u32 dmca = 0;
    u32 ghost = 0;
    u32 ai_sched = 0;
    u32 clroe = 0;
    u32 sec_box = 0;
    u32 hybrid = 0;
    u32 ghost_gpu = 0;
    u32 energy = 0;
    u32 learn = 0;
    u32 quantum_pred = 0;
    pcoe2_core_view_t corev;
    pcoe2_matrix_view_t mv;
    hyper_ghost_status_t hgs;
    quantum_scheduler_status_t qss;
    u32 core_pressure = 0;

    resource_monitor_snapshot(&rs);
    resource_engine_snapshot(&re);
    hal_status(&hs);
    sdc_status(&sdc_st);
    ai_security_status(&ai);
    antivirus_status(&av);
    pcoe2_core_ingest(&rs);
    pcoe2_core_view(&corev);
    pcoe2_matrix_tick(&rs, &re, &hs, &sdc_st, &ai, &av, (u32)st.mode, &mv);
    hyper_ghost_tick(&rs, &re, &hs, (u32)st.mode);
    quantum_scheduler_tick(&rs, &re, &hs, &ai, &av, (u32)st.mode);
    hyper_ghost_status(&hgs);
    quantum_scheduler_status(&qss);
    core_pressure = pcoe2_core_predict_pressure();
    st.core_anomaly_score = (corev.anomaly_score + qss.anomaly) / 2u;
    st.core_stability_score = (corev.stability_score + qss.stability) / 2u;
    st.core_predicted_pressure = (core_pressure + qss.predicted_pressure + hgs.pressure) / 3u;
    st.hyper_ghost_pressure = hgs.pressure;
    st.hyper_ghost_latency_shadow = hgs.latency_shadow;
    st.hyper_ghost_reserved_kb = hgs.reserved_kb;
    st.hyper_ghost_hot_pool_kb = hgs.hot_pool_kb;
    st.hyper_ghost_warm_pool_kb = hgs.warm_pool_kb;
    st.hyper_ghost_cold_pool_kb = hgs.cold_pool_kb;
    st.hyper_ghost_shadow_pages = hgs.shadow_pages;
    st.hyper_ghost_compression_permille = hgs.compression_permille;
    st.hyper_ghost_dedup_permille = hgs.dedup_permille;
    st.hyper_ghost_reclaim_events = hgs.reclaim_events;
    st.hyper_ghost_rebalance_events = hgs.rebalance_events;
    st.quantum_score = qss.score;
    st.quantum_pressure = qss.predicted_pressure;
    st.quantum_anomaly = qss.anomaly;
    st.quantum_stability = qss.stability;
    st.quantum_preempt_hint = qss.preempt_hint;
    st.quantum_throughput_gain = qss.throughput_gain;
    st.quantum_fairness_index = qss.fairness_index;
    st.quantum_predicted_threads = qss.predicted_threads;
    st.quantum_dependency_depth = qss.dependency_depth;
    st.quantum_clone_level = qss.clone_level;
    st.quantum_micro_thread_level = qss.micro_thread_level;

    if (rs.mem_total_bytes) {
        mem_percent = (rs.mem_used_bytes * 100u) / rs.mem_total_bytes;
    } else if (total_ram) {
        mem_percent = (rs.mem_used_bytes * 100u) / total_ram;
    }
    io_pressure = clamp100((rs.io_queue_depth * 4u) + (rs.swap_used_kb / 64u));

    st.security_pressure = clamp100((ai.risk_score / 4u) + (av.active_threats * 10u) + (sdc_st.alerts_total / 2u) + (corev.anomaly_score / 6u));
    st.latency_shadow_level = pcoe2_core_predict_latency_shadow();

    /* 1) PRS 2.0 */
    prs = clamp100((rs.predicted_next_alloc / 8192u) + (rs.recommended_threads * 8u) +
                   (100u - mem_percent) / 2u + (st.latency_shadow_level * 8u));
    /* 2) DMCA 2.0 */
    dmca = clamp100((hs.hetero_core_score / 2u) + (total_cores * 5u) + (corev.predicted_cpu / 3u));
    /* 3) Hyper-Ghost Memory */
    ghost = clamp100((hs.ghost_ram_enabled ? 35u : 5u) + (hs.ghost_ram_reserved_kb / 512u) +
                     (100u - mem_percent) / 2u + (rs.low_mem_warning ? 0u : 10u));
    /* 4) Kernel AI Scheduler */
    ai_sched = clamp100((dmca / 3u) + (prs / 3u) + (100u - io_pressure) / 3u + bias_quantum + (corev.stability_score / 10u));
    /* 5) CLROE 2.0 */
    clroe = clamp100((prs / 4u) + (dmca / 4u) + (ghost / 5u) + (ai_sched / 5u) +
                     (100u - io_pressure) / 5u + (hs.data_prefetch_score / 5u));
    /* 6) Predictive Security Sandbox */
    sec_box = clamp100((100u - st.security_pressure) / 2u + (sdc_st.cpu_safe ? 15u : 0u) +
                       (sdc_st.ram_safe ? 15u : 0u) + (sdc_st.files_integrity_percent / 5u));
    /* 7) Hybrid mode switch readiness */
    hybrid = clamp100((st.mode == PCOE_MODE_AUTO ? 25u : 55u) + (100u - io_pressure) / 4u +
                      (total_cores * 3u));
    /* 8) Ghost GPU/Compute Scheduler */
    ghost_gpu = clamp100((hs.gpu_accel_level * 25u) + (hs.smart_render_level * 15u) +
                         (hs.predicted_threads * 5u));
    /* 9) Energy & thermal awareness */
    energy = clamp100((100u - re.thermal_celsius) + (power_get_mode() == POWER_MODE_LOW_POWER ? 20u : 0u));
    /* 10) Override learning */
    learn = clamp100((bias_quantum + bias_power + bias_ghost + bias_render) * 5u);
    /* 11) Quantum predictive scheduler */
    quantum_pred = clamp100((ai_sched / 2u) + (st.latency_shadow_level * 15u) + (dmca / 4u) + (core_pressure / 6u));

    st.prs_score = (prs + mv.prs_score) / 2u;
    st.dmca_score = (dmca + mv.dmca_score) / 2u;
    st.ghost_score = (ghost + mv.ghost_score + hgs.score) / 3u;
    st.ai_sched_score = (ai_sched + mv.ai_sched_score + qss.score) / 3u;
    st.clroe_score = (clroe + mv.clroe_score) / 2u;
    st.security_sandbox_score = (sec_box + mv.security_sandbox_score) / 2u;
    st.hybrid_mode_score = (hybrid + mv.hybrid_mode_score) / 2u;
    st.ghost_gpu_score = (ghost_gpu + mv.ghost_gpu_score) / 2u;
    st.energy_thermal_score = (energy + mv.energy_thermal_score + qss.stability) / 3u;
    st.handcrafted_learning_score = (learn + mv.handcrafted_learning_score) / 2u;
    st.quantum_predictive_score = (quantum_pred + mv.quantum_predictive_score) / 2u;

    st.predicted_threads = resource_engine_recommend_threads((st.prs_score + st.dmca_score + st.ai_sched_score) / 3u, total_cores, 0);
    st.predicted_threads = pcoe2_core_recommended_threads(st.predicted_threads, total_cores);
    if (mv.predicted_threads > st.predicted_threads) {
        st.predicted_threads = mv.predicted_threads;
    }
    if (qss.predicted_threads > st.predicted_threads) {
        st.predicted_threads = qss.predicted_threads;
    }

    st.predicted_dependency_depth = (mv.predicted_dependency_depth + qss.dependency_depth) / 2u;
    if (st.predicted_dependency_depth == 0u) st.predicted_dependency_depth = 1u;
    if (st.predicted_dependency_depth > 5u) st.predicted_dependency_depth = 5u;
    st.adaptive_clone_level = (mv.adaptive_clone_level + qss.clone_level) / 2u;
    st.micro_thread_level = (mv.micro_thread_level + qss.micro_thread_level) / 2u;

    if (st.mode == PCOE_MODE_AUTO && (override_mask & PCOE_OVR_MODE) == 0u) {
        if (mv.recommended_mode == 3u) st.mode = PCOE_MODE_VPS;
        else if (mv.recommended_mode == 2u) st.mode = PCOE_MODE_SERVER;
        else if (mv.recommended_mode == 1u) st.mode = PCOE_MODE_DESKTOP;
        else if (rs.profile == RE_PROFILE_SERVER_FULL || rs.profile == RE_PROFILE_SERVER_LIGHT || sdc_st.mode == SDC_MODE_SERVER) {
            st.mode = (st.latency_shadow_level >= 2u) ? PCOE_MODE_VPS : PCOE_MODE_SERVER;
        } else st.mode = PCOE_MODE_DESKTOP;
    }

    if ((override_mask & PCOE_OVR_QUANTUM) == 0u) {
        st.scheduler_quantum_level = pcoe2_core_recommended_quantum();
        if (mv.scheduler_quantum_level >= 1u && mv.scheduler_quantum_level <= 3u) {
            st.scheduler_quantum_level = mv.scheduler_quantum_level;
        }
        if (qss.quantum_level >= 1u && qss.quantum_level <= 3u) {
            st.scheduler_quantum_level = qss.quantum_level;
        }
        if (st.security_pressure > 75u || st.latency_shadow_level == 3u) st.scheduler_quantum_level = 1u;
        else if (quantum_pred > 70u || core_pressure > 60u || io_pressure > 40u) st.scheduler_quantum_level = 2u;
        scheduler_set_quantum_level(st.scheduler_quantum_level);
    }

    if ((override_mask & PCOE_OVR_RENDER) == 0u) {
        hal_set_render_policy(mv.recommended_render);
        if (st.security_pressure > 70u || rs.low_mem_warning || re.thermal_celsius > 84u) {
            hal_set_render_policy(HAL_RENDER_PERFORMANCE);
        } else if (st.mode == PCOE_MODE_DESKTOP) {
            hal_set_render_policy(HAL_RENDER_QUALITY);
        } else {
            hal_set_render_policy(HAL_RENDER_AUTO);
        }
    }

    if ((override_mask & PCOE_OVR_GHOST) == 0u) {
        hal_set_ghost_ram(mv.recommended_ghost_ram);
        if (hgs.policy_mode == 2u && hgs.reserved_kb > 8192u) {
            hal_set_ghost_ram(1u);
        }
        if (rs.low_mem_warning || mem_percent > 90u) {
            hal_set_ghost_ram(0u);
        } else {
            hal_set_ghost_ram(1u);
        }
    }

    if ((override_mask & PCOE_OVR_POWER) == 0u) {
        power_set_mode(mv.recommended_power);
        if (re.thermal_celsius > 86u || st.security_pressure > 80u || corev.anomaly_score > 70u) {
            power_set_mode(POWER_MODE_LOW_POWER);
        } else if ((st.mode == PCOE_MODE_SERVER || st.mode == PCOE_MODE_VPS) && corev.predicted_cpu > 50u) {
            power_set_mode(POWER_MODE_PERFORMANCE);
        } else {
            power_set_mode(POWER_MODE_BALANCED);
        }
    }

    apply_stack_policy();

    if (rs.low_mem_warning || re.thermal_celsius > 90u || av.filesystem_integrity_percent < 95u) {
        st.self_heal_level++;
    }

    if (st.owner_lock_open && timer_ticks() > st.owner_lock_expires_tick) {
        st.owner_lock_open = 0;
        override_mask = 0;
    }
}

void pcoe2_status(pcoe2_status_t* out) {
    if (!out) {
        return;
    }
    *out = st;
}

void pcoe2_set_mode(pcoe_mode_t mode) {
    if (mode <= PCOE_MODE_VPS) {
        st.mode = mode;
        apply_stack_policy();
        pcoe2_preload_predictive(st.mode);
    }
}

pcoe_mode_t pcoe2_get_mode(void) {
    return st.mode;
}

int pcoe2_auth_owner(const char* user, const char* token) {
    if (!user || !token) {
        return -1;
    }
    if (str_cmp(user, "taha") != 0 &&
        str_cmp(user, "طاها") != 0 &&
        str_cmp(user, "Ø·Ø§Ù‡Ø§") != 0) {
        return -1;
    }
    if (str_cmp(token, "lastos-pcoe2") != 0) {
        return -1;
    }
    st.owner_lock_open = 1;
    st.owner_lock_expires_tick = timer_ticks() + 7000u;
    return 0;
}

int pcoe2_apply_override(const char* key, u32 value) {
    if (!key || !key[0]) {
        return -1;
    }
    if (!st.owner_lock_open || timer_ticks() > st.owner_lock_expires_tick) {
        st.owner_lock_open = 0;
        override_mask = 0;
        return -2;
    }

    if (str_cmp(key, "mode") == 0) {
        override_mask |= PCOE_OVR_MODE;
        pcoe2_set_mode((value > 3u) ? PCOE_MODE_AUTO : (pcoe_mode_t)value);
        pcoe2_matrix_note_override(0u, 0u, 0u, 0u);
    } else if (str_cmp(key, "quantum") == 0) {
        override_mask |= PCOE_OVR_QUANTUM;
        st.scheduler_quantum_level = (value < 1u) ? 1u : (value > 3u ? 3u : value);
        scheduler_set_quantum_level(st.scheduler_quantum_level);
        bias_quantum = (bias_quantum < 10u) ? (bias_quantum + 1u) : 10u;
        pcoe2_matrix_note_override(1u, 0u, 0u, 0u);
    } else if (str_cmp(key, "render") == 0) {
        override_mask |= PCOE_OVR_RENDER;
        hal_set_render_policy(value == 0u ? HAL_RENDER_AUTO : (value == 1u ? HAL_RENDER_QUALITY : HAL_RENDER_PERFORMANCE));
        bias_render = (bias_render < 10u) ? (bias_render + 1u) : 10u;
        pcoe2_matrix_note_override(0u, 0u, 0u, 1u);
    } else if (str_cmp(key, "ghost") == 0) {
        override_mask |= PCOE_OVR_GHOST;
        hal_set_ghost_ram(value ? 1u : 0u);
        bias_ghost = (bias_ghost < 10u) ? (bias_ghost + 1u) : 10u;
        pcoe2_matrix_note_override(0u, 0u, 1u, 0u);
    } else if (str_cmp(key, "profile") == 0) {
        override_mask |= PCOE_OVR_PROFILE;
        if (value > 4u) value = 0u;
        resource_engine_set_profile((re_profile_t)value);
    } else if (str_cmp(key, "power") == 0) {
        override_mask |= PCOE_OVR_POWER;
        if (value > 3u) value = 0u;
        power_set_mode((power_mode_t)value);
        bias_power = (bias_power < 10u) ? (bias_power + 1u) : 10u;
        pcoe2_matrix_note_override(0u, 1u, 0u, 0u);
    } else {
        return -3;
    }

    st.handcrafted_overrides++;
    return 0;
}

void pcoe2_rollback_overrides(void) {
    override_mask = 0;
    st.handcrafted_overrides = 0;
    st.owner_lock_open = 0;
    st.mode = PCOE_MODE_AUTO;
    st.scheduler_quantum_level = 2u;
    st.predicted_dependency_depth = 1u;
    st.adaptive_clone_level = 0u;
    st.micro_thread_level = 1u;
    scheduler_set_quantum_level(st.scheduler_quantum_level);
}

u32 pcoe2_performance_pressure(void) {
    u32 p = (st.prs_score + st.dmca_score + st.clroe_score + st.core_predicted_pressure) / 4u;
    if (st.security_pressure > 70u) {
        p = (p > 20u) ? (p - 20u) : 0u;
    }
    return clamp100(p);
}

u32 pcoe2_security_scan_budget(u32 base_budget, u32 cpu_busy_percent) {
    u32 p = pcoe2_performance_pressure();
    u32 b = base_budget;
    if (st.mode == PCOE_MODE_SERVER || st.mode == PCOE_MODE_VPS) {
        b += 8u;
    }
    if (st.security_pressure > 70u) {
        b += 12u;
    } else if (st.security_pressure > 40u) {
        b += 6u;
    }
    if (cpu_busy_percent > 85u || st.core_anomaly_score > 75u) {
        b /= 2u;
    } else if (p > 70u && cpu_busy_percent < 70u) {
        b += 4u;
    }
    if (b < 1u) b = 1u;
    if (b > 64u) b = 64u;
    return b;
}

u32 pcoe2_task_risk_threshold(void) {
    u32 th = 64u;
    if (st.mode == PCOE_MODE_SERVER || st.mode == PCOE_MODE_VPS) {
        th = 48u;
    }
    if (st.security_pressure > 70u) {
        th = (th > 20u) ? (th - 20u) : 20u;
    } else if (st.security_pressure > 40u) {
        th = (th > 10u) ? (th - 10u) : 24u;
    }
    if (st.core_anomaly_score > 70u) {
        th = (th > 8u) ? (th - 8u) : 20u;
    }
    return th;
}

u32 pcoe2_scheduler_preempt_hint(void) {
    u32 qh = quantum_scheduler_preempt_hint();
    if (qh > 3u) qh = 3u;
    if (st.latency_shadow_level >= 3u || st.core_predicted_pressure > 80u) {
        if (qh > 2u) return qh;
        return 3u;
    }
    if (st.latency_shadow_level == 2u || st.core_predicted_pressure > 55u) {
        if (qh > 1u) return qh;
        return 2u;
    }
    return qh ? qh : 1u;
}
