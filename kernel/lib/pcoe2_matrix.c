#include "pcoe2_matrix.h"
#include "timer.h"

#define RISK_TABLE 64u
#define DEP_NODES 64u
#define HIST_WINDOW 8u

typedef struct {
    u32 cpu_hist[PCOE2_MATRIX_HIST_LEN];
    u32 mem_hist[PCOE2_MATRIX_HIST_LEN];
    u32 io_hist[PCOE2_MATRIX_HIST_LEN];
    u32 net_hist[PCOE2_MATRIX_HIST_LEN];
    u32 sec_hist[PCOE2_MATRIX_HIST_LEN];
    u32 thermal_hist[PCOE2_MATRIX_HIST_LEN];
    u32 idx;
} signal_ring_t;

typedef struct {
    u32 affinity[PCOE2_MATRIX_MAX_TASKS][PCOE2_MATRIX_MAX_CORES];
    u32 heat[PCOE2_MATRIX_MAX_CORES];
    u32 idle[PCOE2_MATRIX_MAX_CORES];
    u32 top_score;
} dmca_state_t;

typedef struct {
    u32 pool_hot_pages;
    u32 pool_warm_pages;
    u32 pool_cold_pages;
    u32 shadow_pages;
    u32 compression_ratio_permille;
    u32 dedup_ratio_permille;
    u32 rebalance_events;
} ghost_mem_state_t;

typedef struct {
    u32 graph[DEP_NODES][4];
    u32 edge_count[DEP_NODES];
    u32 activity[DEP_NODES];
    u32 depth_prediction;
    u32 clone_level;
    u32 micro_threads;
} qsched_state_t;

typedef struct {
    u32 module_risk[RISK_TABLE];
    u32 quarantine_bias[RISK_TABLE];
    u32 anomalies;
    u32 safe_score;
} sandbox_state_t;

typedef struct {
    u32 gpu_compute_queue;
    u32 gfx_queue;
    u32 offload_gain;
    u32 render_level;
} ghost_gpu_state_t;

typedef struct {
    u32 perf_bias;
    u32 bal_bias;
    u32 low_bias;
    u32 thermal_budget;
    u32 energy_score;
} energy_state_t;

typedef struct {
    u32 total_ram;
    u32 total_cores;
    u32 ticks;

    signal_ring_t sig;
    dmca_state_t dmca;
    ghost_mem_state_t ghost;
    qsched_state_t qs;
    sandbox_state_t sb;
    ghost_gpu_state_t gpu;
    energy_state_t en;

    u32 override_quantum;
    u32 override_power;
    u32 override_ghost;
    u32 override_render;

    pcoe2_matrix_view_t view;
} matrix_state_t;

static matrix_state_t g;

static const u32 mode_weight_prs[4][32] = {
    {20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20},
    {24,24,22,22,24,24,22,22,24,24,22,22,24,24,22,22,24,24,22,22,24,24,22,22,24,24,22,22,24,24,22,22},
    {28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26},
    {30,30,28,28,30,30,28,28,30,30,28,28,30,30,28,28,30,30,28,28,30,30,28,28,30,30,28,28,30,30,28,28}
};

static const u32 mode_weight_energy[4][32] = {
    {18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18,18},
    {20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20},
    {26,26,24,24,26,26,24,24,26,26,24,24,26,26,24,24,26,26,24,24,26,26,24,24,26,26,24,24,26,26,24,24},
    {28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26,28,28,26,26}
};

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 clamp_core(u32 c) {
    if (c == 0u) return 1u;
    if (c > PCOE2_MATRIX_MAX_CORES) return PCOE2_MATRIX_MAX_CORES;
    return c;
}

static u32 sidx(u32 back) {
    if (back >= PCOE2_MATRIX_HIST_LEN) back = PCOE2_MATRIX_HIST_LEN - 1u;
    return (g.sig.idx + PCOE2_MATRIX_HIST_LEN - back) % PCOE2_MATRIX_HIST_LEN;
}

static u32 slope_last(const u32* v, u32 n) {
    if (n < 2u) return 0u;
    if (n > PCOE2_MATRIX_HIST_LEN) n = PCOE2_MATRIX_HIST_LEN;
    u32 older = v[sidx(n - 1u)];
    u32 newer = v[sidx(0u)];
    return (newer > older) ? (newer - older) : 0u;
}

static u32 weighted_recent(const u32* v) {
    u32 a = v[sidx(0u)] * 6u;
    u32 b = v[sidx(1u)] * 5u;
    u32 c = v[sidx(2u)] * 4u;
    u32 d = v[sidx(3u)] * 3u;
    u32 e = v[sidx(4u)] * 2u;
    u32 f = v[sidx(5u)] * 1u;
    return (a + b + c + d + e + f) / 21u;
}

static void push_signals(const resource_snapshot_t* rs, const resource_engine_snapshot_t* re,
                         const ai_security_status_t* ai, const antivirus_status_t* av) {
    u32 i = g.sig.idx;
    g.sig.cpu_hist[i] = rs->cpu_busy_percent;
    g.sig.mem_hist[i] = rs->mem_total_bytes ? (rs->mem_used_bytes * 100u) / rs->mem_total_bytes : 0u;
    g.sig.io_hist[i] = clamp100((rs->io_queue_depth * 4u) + (rs->swap_used_kb / 64u));
    g.sig.net_hist[i] = clamp100(rs->net_packets / 10u);
    g.sig.sec_hist[i] = clamp100((ai->risk_score / 4u) + (av->active_threats * 10u));
    g.sig.thermal_hist[i] = re->thermal_celsius;
    g.sig.idx = (i + 1u) % PCOE2_MATRIX_HIST_LEN;
}

static u32 prs_predict_pressure(u32 mode) {
    u32 cpu_w = weighted_recent(g.sig.cpu_hist);
    u32 mem_w = weighted_recent(g.sig.mem_hist);
    u32 io_w = weighted_recent(g.sig.io_hist);
    u32 net_w = weighted_recent(g.sig.net_hist);
    u32 cpu_s = slope_last(g.sig.cpu_hist, HIST_WINDOW);
    u32 mem_s = slope_last(g.sig.mem_hist, HIST_WINDOW);
    u32 io_s = slope_last(g.sig.io_hist, HIST_WINDOW);

    u32 w = mode_weight_prs[(mode > 3u) ? 0u : mode][g.ticks % 32u];
    u32 base = (cpu_w * 3u + mem_w * 3u + io_w * 2u + net_w) / 9u;
    u32 trend = clamp100((cpu_s + mem_s + io_s) / 3u);
    u32 pressure = clamp100((base * 7u + trend * 3u) / 10u + (w / 4u));
    return pressure;
}

static u32 prs_latency_shadow(u32 pressure) {
    if (pressure > 78u) return 3u;
    if (pressure > 52u) return 2u;
    return 1u;
}

static u32 prs_predicted_threads(u32 pressure) {
    u32 cores = clamp_core(g.total_cores);
    if (pressure > 85u) return cores;
    if (pressure > 60u) return (cores > 2u) ? (cores - 1u) : cores;
    if (pressure > 35u) return (cores / 2u) ? (cores / 2u) : 1u;
    return 1u;
}

static void dmca_reset(void) {
    for (u32 t = 0; t < PCOE2_MATRIX_MAX_TASKS; t++) {
        for (u32 c = 0; c < PCOE2_MATRIX_MAX_CORES; c++) {
            g.dmca.affinity[t][c] = 0u;
        }
    }
    for (u32 c = 0; c < PCOE2_MATRIX_MAX_CORES; c++) {
        g.dmca.heat[c] = 0u;
        g.dmca.idle[c] = 0u;
    }
    g.dmca.top_score = 0u;
}

static void dmca_update(const resource_snapshot_t* rs, const hal_status_t* hs, u32 pressure) {
    u32 cores = clamp_core(g.total_cores);
    for (u32 c = 0; c < cores; c++) {
        u32 thermal_bias = (hs->thermal_guard_level > c) ? (hs->thermal_guard_level - c) : 0u;
        u32 load = clamp100((rs->cpu_busy_percent + c * 7u) % 100u);
        g.dmca.heat[c] = clamp100((load * 6u + thermal_bias * 4u) / 10u);
        g.dmca.idle[c] = 100u - g.dmca.heat[c];
    }

    for (u32 t = 0; t < PCOE2_MATRIX_MAX_TASKS; t++) {
        for (u32 c = 0; c < cores; c++) {
            u32 locality = ((t * 11u + c * 17u + g.ticks) % 100u);
            u32 thermal_penalty = g.dmca.heat[c] / 3u;
            u32 score = (locality > thermal_penalty) ? (locality - thermal_penalty) : 0u;
            if (pressure > 70u && c == (t % cores)) {
                score = clamp100(score + 20u);
            }
            g.dmca.affinity[t][c] = score;
            if (score > g.dmca.top_score) g.dmca.top_score = score;
        }
    }
}

static u32 dmca_score(void) {
    u32 cores = clamp_core(g.total_cores);
    u32 sum = 0u;
    for (u32 c = 0; c < cores; c++) sum += g.dmca.idle[c];
    u32 balance = sum / cores;
    return clamp100((g.dmca.top_score / 2u) + (balance / 2u));
}

static void ghost_mem_update(const resource_snapshot_t* rs, const hal_status_t* hs, u32 pressure) {
    u32 mem_pct = rs->mem_total_bytes ? (rs->mem_used_bytes * 100u) / rs->mem_total_bytes : 0u;
    u32 free_pct = (mem_pct > 100u) ? 0u : (100u - mem_pct);

    g.ghost.pool_hot_pages = (free_pct * 256u) / 100u;
    g.ghost.pool_warm_pages = (free_pct * 512u) / 100u;
    g.ghost.pool_cold_pages = (free_pct * 768u) / 100u;

    g.ghost.shadow_pages = (pressure > 60u) ? (g.ghost.pool_warm_pages / 2u) : (g.ghost.pool_cold_pages / 4u);
    g.ghost.compression_ratio_permille = 900u + (free_pct * 2u);
    g.ghost.dedup_ratio_permille = 850u + (hs->data_prefetch_score * 2u);

    if (rs->low_mem_warning) {
        g.ghost.rebalance_events++;
        g.ghost.pool_hot_pages /= 2u;
        g.ghost.pool_warm_pages /= 2u;
        g.ghost.shadow_pages /= 2u;
    }
}

static u32 ghost_mem_score(void) {
    u32 pool_score = clamp100((g.ghost.pool_hot_pages + g.ghost.pool_warm_pages / 2u) / 8u);
    u32 comp = (g.ghost.compression_ratio_permille > 1000u) ? 100u : (g.ghost.compression_ratio_permille / 10u);
    u32 ded = (g.ghost.dedup_ratio_permille > 1000u) ? 100u : (g.ghost.dedup_ratio_permille / 10u);
    return clamp100((pool_score * 5u + comp * 3u + ded * 2u) / 10u);
}

static void qsched_reset_graph(void) {
    for (u32 n = 0; n < DEP_NODES; n++) {
        g.qs.edge_count[n] = 0u;
        g.qs.activity[n] = 0u;
        for (u32 e = 0; e < 4u; e++) g.qs.graph[n][e] = n;
    }
    g.qs.depth_prediction = 1u;
    g.qs.clone_level = 0u;
    g.qs.micro_threads = 1u;
}

static void qsched_build_graph(u32 pressure, u32 dmca, u32 prs) {
    for (u32 n = 0; n < DEP_NODES; n++) {
        u32 edge_n = 1u + ((n + g.ticks + pressure) % 4u);
        g.qs.edge_count[n] = edge_n;
        g.qs.activity[n] = clamp100((prs + dmca + n) % 100u);
        for (u32 e = 0; e < edge_n; e++) {
            g.qs.graph[n][e] = (n + e + 1u + (pressure % 5u)) % DEP_NODES;
        }
    }

    u32 active_nodes = 0u;
    for (u32 n = 0; n < DEP_NODES; n++) {
        if (g.qs.activity[n] > 60u) active_nodes++;
    }
    g.qs.depth_prediction = 1u + (active_nodes / 18u);
    if (g.qs.depth_prediction > 5u) g.qs.depth_prediction = 5u;

    g.qs.clone_level = (pressure > 70u) ? (1u + pressure / 20u) : 0u;
    if (g.qs.clone_level > 4u) g.qs.clone_level = 4u;

    g.qs.micro_threads = 1u + (pressure / 35u);
    if (g.qs.micro_threads > 4u) g.qs.micro_threads = 4u;
}

static u32 qsched_score(u32 pressure, u32 dmca, u32 ghost) {
    u32 graph_quality = clamp100((g.qs.depth_prediction * 18u) + (g.qs.micro_threads * 12u));
    u32 blend = (pressure + dmca + ghost) / 3u;
    return clamp100((graph_quality * 4u + blend * 6u) / 10u);
}

static void sandbox_update(const sdc_status_t* sdc_st, const ai_security_status_t* ai,
                           const antivirus_status_t* av, u32 pressure) {
    u32 sec = clamp100((ai->risk_score / 4u) + (av->active_threats * 8u) + (sdc_st->alerts_total / 2u));

    g.sb.anomalies = 0u;
    for (u32 i = 0; i < RISK_TABLE; i++) {
        u32 base = (i * 7u + g.ticks + sec) % 100u;
        u32 risk = clamp100((base * 6u + sec * 4u) / 10u);
        if (pressure > 75u && (i % 3u == 0u)) risk = clamp100(risk + 10u);
        g.sb.module_risk[i] = risk;
        g.sb.quarantine_bias[i] = (risk > 65u) ? (risk - 65u) : 0u;
        if (risk > 80u) g.sb.anomalies++;
    }

    u32 safe = 100u;
    if (g.sb.anomalies > 40u) safe = 25u;
    else if (g.sb.anomalies > 20u) safe = 45u;
    else if (g.sb.anomalies > 8u) safe = 65u;
    g.sb.safe_score = safe;
}

static u32 sandbox_score(void) {
    return g.sb.safe_score;
}

static void gpu_update(const hal_status_t* hs, const resource_snapshot_t* rs, u32 pressure) {
    g.gpu.gpu_compute_queue = clamp100((hs->gpu_accel_level * 22u) + (pressure / 2u));
    g.gpu.gfx_queue = clamp100((hs->smart_render_level * 25u) + (rs->cpu_busy_percent / 3u));
    g.gpu.offload_gain = clamp100((g.gpu.gpu_compute_queue + g.gpu.gfx_queue) / 2u + hs->predicted_threads * 3u);

    if (pressure > 75u) g.gpu.render_level = 1u;
    else if (pressure > 45u) g.gpu.render_level = 2u;
    else g.gpu.render_level = 3u;
}

static u32 gpu_score(void) {
    return clamp100((g.gpu.offload_gain * 7u + g.gpu.render_level * 10u) / 8u);
}

static void energy_update(u32 mode, const resource_engine_snapshot_t* re,
                          const resource_snapshot_t* rs, const hal_status_t* hs) {
    u32 w = mode_weight_energy[(mode > 3u) ? 0u : mode][g.ticks % 32u];
    u32 thermal = re->thermal_celsius;
    u32 thermal_penalty = (thermal > 90u) ? 70u : (thermal > 80u ? 40u : (thermal > 70u ? 20u : 5u));

    g.en.perf_bias = clamp100((rs->cpu_busy_percent / 2u) + (hs->hetero_core_score / 2u));
    g.en.bal_bias = clamp100((100u - thermal_penalty) + (w / 3u));
    g.en.low_bias = clamp100((thermal_penalty) + (rs->low_mem_warning ? 15u : 0u));
    g.en.thermal_budget = (thermal > 100u) ? 0u : (100u - thermal);
    g.en.energy_score = clamp100((g.en.bal_bias * 5u + g.en.thermal_budget * 5u) / 10u);
}

static power_mode_t energy_pick_mode(u32 pressure, u32 sec_score) {
    if (g.en.low_bias > 70u || g.en.thermal_budget < 12u || sec_score < 40u) {
        return POWER_MODE_LOW_POWER;
    }
    if (pressure > 62u || g.en.perf_bias > 65u) {
        return POWER_MODE_PERFORMANCE;
    }
    return POWER_MODE_BALANCED;
}

static u32 energy_score(void) {
    return g.en.energy_score;
}

static hal_render_policy_t pick_render(u32 mode, u32 pressure, u32 sec_score) {
    if (sec_score < 40u || pressure > 80u) return HAL_RENDER_PERFORMANCE;
    if (mode == 1u && pressure < 55u) return HAL_RENDER_QUALITY;
    return HAL_RENDER_AUTO;
}

static u8 pick_ghost_ram(u32 pressure, const resource_snapshot_t* rs) {
    if (rs->low_mem_warning) return 0u;
    if (pressure > 88u) return 0u;
    return 1u;
}

static u8 pick_mode(u32 mode, const resource_snapshot_t* rs, const sdc_status_t* sdc_st, u32 pressure) {
    if (mode != 0u) return (u8)mode;
    if (sdc_st->mode == SDC_MODE_SERVER) {
        return (pressure > 62u) ? 3u : 2u;
    }
    if (rs->profile == RE_PROFILE_SERVER_FULL || rs->profile == RE_PROFILE_SERVER_LIGHT) {
        return (pressure > 62u) ? 3u : 2u;
    }
    return 1u;
}

static u32 compute_learning(void) {
    u32 sum = g.override_quantum + g.override_power + g.override_ghost + g.override_render;
    return clamp100(sum * 6u);
}

static u32 compute_stability(void) {
    u32 cpu_var = slope_last(g.sig.cpu_hist, HIST_WINDOW);
    u32 mem_var = slope_last(g.sig.mem_hist, HIST_WINDOW);
    u32 io_var = slope_last(g.sig.io_hist, HIST_WINDOW);
    u32 sec_var = slope_last(g.sig.sec_hist, HIST_WINDOW);
    u32 unstable = clamp100((cpu_var + mem_var + io_var + sec_var) / 4u);
    return 100u - unstable;
}

static u32 compute_anomaly(u32 pressure, u32 sec_score) {
    u32 sec_recent = weighted_recent(g.sig.sec_hist);
    u32 io_recent = weighted_recent(g.sig.io_hist);
    u32 th_recent = weighted_recent(g.sig.thermal_hist);
    u32 signal = clamp100((sec_recent * 5u + io_recent * 3u + th_recent * 2u) / 10u);
    u32 blend = clamp100((signal + pressure + (100u - sec_score)) / 3u);
    return blend;
}

static u32 compute_clroe(u32 prs, u32 dmca, u32 ghost, u32 ai_sched, u32 sec_box, u32 energy) {
    return clamp100((prs * 2u + dmca * 2u + ghost + ai_sched * 2u + sec_box + energy * 2u) / 10u);
}

static u32 compute_quantum(u32 pressure, u32 anomaly, u32 sec_score) {
    if (anomaly > 80u || sec_score < 35u) return 1u;
    if (pressure > 65u) return 2u;
    return 3u;
}

static u32 compute_preempt_hint(u32 pressure, u32 anomaly) {
    if (pressure > 78u || anomaly > 72u) return 3u;
    if (pressure > 50u || anomaly > 45u) return 2u;
    return 1u;
}

static u32 compute_risk_threshold(u32 sec_score, u32 anomaly) {
    u32 th = 64u;
    if (sec_score < 70u) th -= 10u;
    if (sec_score < 50u) th -= 10u;
    if (anomaly > 70u) th -= 10u;
    if (th < 20u) th = 20u;
    return th;
}

static u32 compute_scan_boost(u32 sec_score, u32 pressure, u32 anomaly) {
    u32 b = 0u;
    if (sec_score < 80u) b += 4u;
    if (sec_score < 60u) b += 6u;
    if (pressure > 70u) b += 4u;
    if (anomaly > 70u) b += 8u;
    return b;
}

static u32 score_prs_from_pressure(u32 pressure, u32 latency, u32 threads) {
    u32 s = clamp100((pressure * 6u + latency * 14u + threads * 8u) / 10u);
    return s;
}

static u32 score_hybrid(u32 picked_mode, u32 clroe) {
    u32 base = (picked_mode == 3u || picked_mode == 2u) ? 78u : 62u;
    return clamp100((base * 6u + clroe * 4u) / 10u);
}

static u32 score_quantum_predictive(u32 q, u32 depth, u32 clone, u32 micro) {
    u32 s = (q * 20u) + (depth * 10u) + (clone * 10u) + (micro * 10u);
    return clamp100(s);
}

static void fill_output(u32 mode,
                        u32 pressure,
                        u32 prs,
                        u32 dmca,
                        u32 ghost,
                        u32 ai_sched,
                        u32 clroe,
                        u32 sec_box,
                        u32 hybrid,
                        u32 gpu,
                        u32 energy,
                        u32 learning,
                        u32 qpred,
                        u32 latency,
                        u32 threads,
                        u32 quantum,
                        u32 depth,
                        u32 clone,
                        u32 micro,
                        u32 preempt,
                        u32 anomaly,
                        u32 stability,
                        u32 risk_th,
                        u32 scan_boost,
                        power_mode_t power_mode,
                        hal_render_policy_t render,
                        u8 ghost_ram,
                        u8 rec_mode,
                        pcoe2_matrix_view_t* out) {
    out->prs_score = prs;
    out->dmca_score = dmca;
    out->ghost_score = ghost;
    out->ai_sched_score = ai_sched;
    out->clroe_score = clroe;
    out->security_sandbox_score = sec_box;
    out->hybrid_mode_score = hybrid;
    out->ghost_gpu_score = gpu;
    out->energy_thermal_score = energy;
    out->handcrafted_learning_score = learning;
    out->quantum_predictive_score = qpred;

    out->latency_shadow_level = latency;
    out->predicted_threads = threads;
    out->scheduler_quantum_level = quantum;
    out->predicted_dependency_depth = depth;
    out->adaptive_clone_level = clone;
    out->micro_thread_level = micro;

    out->preempt_hint = preempt;
    out->predicted_pressure = pressure;
    out->anomaly_score = anomaly;
    out->stability_score = stability;
    out->risk_threshold = risk_th;
    out->security_scan_boost = scan_boost;

    out->recommended_power = power_mode;
    out->recommended_render = render;
    out->recommended_ghost_ram = ghost_ram;
    out->recommended_mode = rec_mode;

    (void)mode;
}

void pcoe2_matrix_init(u32 total_ram_bytes, u32 total_cores) {
    g.total_ram = total_ram_bytes;
    g.total_cores = clamp_core(total_cores);
    g.ticks = 0u;
    g.sig.idx = 0u;

    for (u32 i = 0; i < PCOE2_MATRIX_HIST_LEN; i++) {
        g.sig.cpu_hist[i] = 0u;
        g.sig.mem_hist[i] = 0u;
        g.sig.io_hist[i] = 0u;
        g.sig.net_hist[i] = 0u;
        g.sig.sec_hist[i] = 0u;
        g.sig.thermal_hist[i] = 0u;
    }

    dmca_reset();
    qsched_reset_graph();

    g.ghost.pool_hot_pages = 0u;
    g.ghost.pool_warm_pages = 0u;
    g.ghost.pool_cold_pages = 0u;
    g.ghost.shadow_pages = 0u;
    g.ghost.compression_ratio_permille = 1000u;
    g.ghost.dedup_ratio_permille = 1000u;
    g.ghost.rebalance_events = 0u;

    for (u32 i = 0; i < RISK_TABLE; i++) {
        g.sb.module_risk[i] = 0u;
        g.sb.quarantine_bias[i] = 0u;
    }
    g.sb.anomalies = 0u;
    g.sb.safe_score = 100u;

    g.gpu.gpu_compute_queue = 0u;
    g.gpu.gfx_queue = 0u;
    g.gpu.offload_gain = 0u;
    g.gpu.render_level = 2u;

    g.en.perf_bias = 0u;
    g.en.bal_bias = 0u;
    g.en.low_bias = 0u;
    g.en.thermal_budget = 100u;
    g.en.energy_score = 100u;

    g.override_quantum = 0u;
    g.override_power = 0u;
    g.override_ghost = 0u;
    g.override_render = 0u;

    g.view.prs_score = 0u;
    g.view.dmca_score = 0u;
    g.view.ghost_score = 0u;
    g.view.ai_sched_score = 0u;
    g.view.clroe_score = 0u;
    g.view.security_sandbox_score = 0u;
    g.view.hybrid_mode_score = 0u;
    g.view.ghost_gpu_score = 0u;
    g.view.energy_thermal_score = 0u;
    g.view.handcrafted_learning_score = 0u;
    g.view.quantum_predictive_score = 0u;
    g.view.latency_shadow_level = 1u;
    g.view.predicted_threads = 1u;
    g.view.scheduler_quantum_level = 2u;
    g.view.predicted_dependency_depth = 1u;
    g.view.adaptive_clone_level = 0u;
    g.view.micro_thread_level = 1u;
    g.view.preempt_hint = 1u;
    g.view.predicted_pressure = 0u;
    g.view.anomaly_score = 0u;
    g.view.stability_score = 100u;
    g.view.risk_threshold = 64u;
    g.view.security_scan_boost = 0u;
    g.view.recommended_power = POWER_MODE_BALANCED;
    g.view.recommended_render = HAL_RENDER_AUTO;
    g.view.recommended_ghost_ram = 1u;
    g.view.recommended_mode = 0u;
}

void pcoe2_matrix_note_override(u32 quantum, u32 power, u32 ghost, u32 render) {
    if (quantum && g.override_quantum < 15u) g.override_quantum++;
    if (power && g.override_power < 15u) g.override_power++;
    if (ghost && g.override_ghost < 15u) g.override_ghost++;
    if (render && g.override_render < 15u) g.override_render++;
}

void pcoe2_matrix_tick(const resource_snapshot_t* rs,
                       const resource_engine_snapshot_t* re,
                       const hal_status_t* hs,
                       const sdc_status_t* sdc_st,
                       const ai_security_status_t* ai,
                       const antivirus_status_t* av,
                       u32 mode,
                       pcoe2_matrix_view_t* out) {
    if (!rs || !re || !hs || !sdc_st || !ai || !av || !out) {
        return;
    }

    g.ticks = timer_ticks();

    push_signals(rs, re, ai, av);

    u32 pressure = prs_predict_pressure(mode);
    u32 latency = prs_latency_shadow(pressure);
    u32 threads = prs_predicted_threads(pressure);

    dmca_update(rs, hs, pressure);
    u32 dmca = dmca_score();

    ghost_mem_update(rs, hs, pressure);
    u32 ghost = ghost_mem_score();

    qsched_build_graph(pressure, dmca, clamp100(pressure));
    u32 ai_sched = qsched_score(pressure, dmca, ghost);

    sandbox_update(sdc_st, ai, av, pressure);
    u32 sec_box = sandbox_score();

    gpu_update(hs, rs, pressure);
    u32 gpu = gpu_score();

    energy_update(mode, re, rs, hs);
    u32 energy = energy_score();

    u32 prs = score_prs_from_pressure(pressure, latency, threads);
    u32 learning = compute_learning();

    u32 clroe = compute_clroe(prs, dmca, ghost, ai_sched, sec_box, energy);
    u8 rec_mode = pick_mode(mode, rs, sdc_st, pressure);
    u32 hybrid = score_hybrid(rec_mode, clroe);

    u32 stability = compute_stability();
    u32 anomaly = compute_anomaly(pressure, sec_box);

    u32 quantum = compute_quantum(pressure, anomaly, sec_box);
    u32 depth = g.qs.depth_prediction;
    u32 clone = g.qs.clone_level;
    u32 micro = g.qs.micro_threads;

    u32 qpred = score_quantum_predictive(quantum, depth, clone, micro);

    u32 preempt = compute_preempt_hint(pressure, anomaly);
    u32 risk_th = compute_risk_threshold(sec_box, anomaly);
    u32 scan_boost = compute_scan_boost(sec_box, pressure, anomaly);

    power_mode_t pwr = energy_pick_mode(pressure, sec_box);
    hal_render_policy_t render = pick_render(rec_mode, pressure, sec_box);
    u8 ghost_ram = pick_ghost_ram(pressure, rs);

    fill_output(mode, pressure, prs, dmca, ghost, ai_sched, clroe, sec_box, hybrid,
                gpu, energy, learning, qpred, latency, threads, quantum, depth,
                clone, micro, preempt, anomaly, stability, risk_th, scan_boost,
                pwr, render, ghost_ram, rec_mode, &g.view);

    *out = g.view;
}
