#ifndef PCOE2_H
#define PCOE2_H

#include "types.h"
#include "cpu_detect.h"

typedef enum {
    PCOE_MODE_AUTO = 0,
    PCOE_MODE_DESKTOP = 1,
    PCOE_MODE_SERVER = 2,
    PCOE_MODE_VPS = 3
} pcoe_mode_t;

typedef struct {
    pcoe_mode_t mode;
    /* 1) Predictive Resource Shadowing */
    u32 prs_score;
    /* 2) Dynamic Multi-Core Affinity */
    u32 dmca_score;
    /* 3) Hyper-Ghost Memory */
    u32 ghost_score;
    /* 4) Kernel-Integrated AI Scheduler */
    u32 ai_sched_score;
    /* 5) Cross-Layer Resource Optimization */
    u32 clroe_score;
    /* 6) Predictive Security Sandbox */
    u32 security_sandbox_score;
    /* 7) Hybrid Desktop/Server/VPS Switching */
    u32 hybrid_mode_score;
    /* 8) Ghost GPU/Compute Scheduler */
    u32 ghost_gpu_score;
    /* 9) Energy & Thermal Awareness */
    u32 energy_thermal_score;
    /* 10) Handcrafted Override & Learning */
    u32 handcrafted_learning_score;
    /* 11) Quantum Predictive Scheduler */
    u32 quantum_predictive_score;
    u32 core_anomaly_score;
    u32 core_stability_score;
    u32 core_predicted_pressure;
    u32 hyper_ghost_pressure;
    u32 hyper_ghost_latency_shadow;
    u32 hyper_ghost_reserved_kb;
    u32 hyper_ghost_hot_pool_kb;
    u32 hyper_ghost_warm_pool_kb;
    u32 hyper_ghost_cold_pool_kb;
    u32 hyper_ghost_shadow_pages;
    u32 hyper_ghost_compression_permille;
    u32 hyper_ghost_dedup_permille;
    u32 hyper_ghost_reclaim_events;
    u32 hyper_ghost_rebalance_events;
    u32 quantum_score;
    u32 quantum_pressure;
    u32 quantum_anomaly;
    u32 quantum_stability;
    u32 quantum_preempt_hint;
    u32 quantum_throughput_gain;
    u32 quantum_fairness_index;
    u32 quantum_predicted_threads;
    u32 quantum_dependency_depth;
    u32 quantum_clone_level;
    u32 quantum_micro_thread_level;

    u32 latency_shadow_level;
    u32 predicted_threads;
    u32 scheduler_quantum_level;
    u32 predicted_dependency_depth;
    u32 adaptive_clone_level;
    u32 micro_thread_level;
    u32 preload_level;
    u32 self_heal_level;
    u32 security_pressure;
    u32 owner_lock_open;
    u32 owner_lock_expires_tick;
    u32 handcrafted_overrides;
} pcoe2_status_t;

void pcoe2_init(u32 ram_bytes, u32 cores, const cpu_info_t* cpu);
void pcoe2_tick(void);
void pcoe2_status(pcoe2_status_t* out);
void pcoe2_set_mode(pcoe_mode_t mode);
pcoe_mode_t pcoe2_get_mode(void);
int pcoe2_auth_owner(const char* user, const char* token);
int pcoe2_apply_override(const char* key, u32 value);
void pcoe2_rollback_overrides(void);
void pcoe2_preload_predictive(pcoe_mode_t mode);
u32 pcoe2_performance_pressure(void);
u32 pcoe2_security_scan_budget(u32 base_budget, u32 cpu_busy_percent);
u32 pcoe2_task_risk_threshold(void);
u32 pcoe2_scheduler_preempt_hint(void);

#endif
