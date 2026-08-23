#ifndef PCOE2_MATRIX_H
#define PCOE2_MATRIX_H

#include "types.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "hal.h"
#include "sdc.h"
#include "ai_security.h"
#include "antivirus.h"
#include "power.h"

#define PCOE2_MATRIX_MAX_CORES 128u
#define PCOE2_MATRIX_MAX_TASKS 1024u
#define PCOE2_MATRIX_HIST_LEN 64u

typedef struct {
    u32 prs_score;
    u32 dmca_score;
    u32 ghost_score;
    u32 ai_sched_score;
    u32 clroe_score;
    u32 security_sandbox_score;
    u32 hybrid_mode_score;
    u32 ghost_gpu_score;
    u32 energy_thermal_score;
    u32 handcrafted_learning_score;
    u32 quantum_predictive_score;

    u32 latency_shadow_level;
    u32 predicted_threads;
    u32 scheduler_quantum_level;
    u32 predicted_dependency_depth;
    u32 adaptive_clone_level;
    u32 micro_thread_level;

    u32 preempt_hint;
    u32 predicted_pressure;
    u32 anomaly_score;
    u32 stability_score;
    u32 risk_threshold;
    u32 security_scan_boost;

    power_mode_t recommended_power;
    hal_render_policy_t recommended_render;
    u8 recommended_ghost_ram;
    u8 recommended_mode; /* 0:auto 1:desktop 2:server 3:vps */
} pcoe2_matrix_view_t;

void pcoe2_matrix_init(u32 total_ram_bytes, u32 total_cores);
void pcoe2_matrix_note_override(u32 quantum, u32 power, u32 ghost, u32 render);
void pcoe2_matrix_tick(const resource_snapshot_t* rs,
                       const resource_engine_snapshot_t* re,
                       const hal_status_t* hs,
                       const sdc_status_t* sdc_st,
                       const ai_security_status_t* ai,
                       const antivirus_status_t* av,
                       u32 mode,
                       pcoe2_matrix_view_t* out);

#endif
