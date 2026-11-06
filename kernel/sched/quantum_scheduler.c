#include "quantum_scheduler.h"
#include "timer.h"

#define QS_HIST 128u
#define QS_TASKS 64u
#define QS_CORES 16u
#define QS_GRAPH 96u
#define QS_WIN 16u

typedef struct {
    u32 cpu;
    u32 mem;
    u32 io;
    u32 net;
    u32 thermal;
    u32 sec;
    u32 tick;
} qs_sample_t;

typedef struct {
    u32 id;
    u32 class_hint;
    u32 runtime;
    u32 wakeups;
    u32 blocking;
    u32 affinity[QS_CORES];
    u32 locality;
    u32 criticality;
    u32 risk;
    u32 starvation;
    u32 score;
} qs_task_t;

typedef struct {
    u32 id;
    u32 load;
    u32 thermal;
    u32 idle;
    u32 queue;
    u32 cache_locality;
    u32 energy_cost;
    u32 score;
} qs_core_t;

typedef struct {
    u32 edges[QS_GRAPH][4];
    u32 edge_count[QS_GRAPH];
    u32 active[QS_GRAPH];
    u32 depth;
    u32 breadth;
    u32 entropy;
} qs_graph_t;

typedef struct {
    u32 risk_window[QS_WIN];
    u32 idx;
    u32 momentum;
    u32 anomaly;
    u32 stability;
} qs_risk_t;

typedef struct {
    u32 total_cores;
    u32 tick;
    u32 mode;

    qs_sample_t hist[QS_HIST];
    u32 hist_idx;

    qs_task_t tasks[QS_TASKS];
    qs_core_t cores[QS_CORES];
    qs_graph_t graph;
    qs_risk_t risk;

    quantum_scheduler_status_t st;
} qs_state_t;

static qs_state_t g;

static const u16 qs_mode_quantum_bias[4][128] = {
    {2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
     2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
     2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
     2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2},
    {2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,
     2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,
     2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,
     2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3,2,2,2,2,3,3,3,3},
    {1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,
     1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,
     1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,
     1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2,1,1,2,2},
    {1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,
     1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,
     1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,
     1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2,1,1,1,2}
};

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 min_u32(u32 a, u32 b) {
    return (a < b) ? a : b;
}

static u32 max_u32(u32 a, u32 b) {
    return (a > b) ? a : b;
}

static u32 core_cap(u32 c) {
    if (c == 0u) return 1u;
    if (c > QS_CORES) return QS_CORES;
    return c;
}

static u32 hist_at(u32 back) {
    if (back >= QS_HIST) back = QS_HIST - 1u;
    return (g.hist_idx + QS_HIST - back) % QS_HIST;
}

static u32 avg_field_cpu(u32 n) {
    if (!n) return 0u;
    if (n > QS_HIST) n = QS_HIST;
    u32 s = 0u;
    for (u32 i = 0; i < n; i++) s += g.hist[hist_at(i)].cpu;
    return s / n;
}

static u32 avg_field_mem(u32 n) {
    if (!n) return 0u;
    if (n > QS_HIST) n = QS_HIST;
    u32 s = 0u;
    for (u32 i = 0; i < n; i++) s += g.hist[hist_at(i)].mem;
    return s / n;
}

static u32 avg_field_io(u32 n) {
    if (!n) return 0u;
    if (n > QS_HIST) n = QS_HIST;
    u32 s = 0u;
    for (u32 i = 0; i < n; i++) s += g.hist[hist_at(i)].io;
    return s / n;
}

static u32 avg_field_sec(u32 n) {
    if (!n) return 0u;
    if (n > QS_HIST) n = QS_HIST;
    u32 s = 0u;
    for (u32 i = 0; i < n; i++) s += g.hist[hist_at(i)].sec;
    return s / n;
}

static u32 slope_cpu(u32 n) {
    if (n < 2u) return 0u;
    if (n > QS_HIST) n = QS_HIST;
    u32 newer = g.hist[hist_at(0u)].cpu;
    u32 older = g.hist[hist_at(n - 1u)].cpu;
    return (newer > older) ? (newer - older) : 0u;
}

static u32 slope_io(u32 n) {
    if (n < 2u) return 0u;
    if (n > QS_HIST) n = QS_HIST;
    u32 newer = g.hist[hist_at(0u)].io;
    u32 older = g.hist[hist_at(n - 1u)].io;
    return (newer > older) ? (newer - older) : 0u;
}

static void push_sample(const resource_snapshot_t* rs, const resource_engine_snapshot_t* re,
                        const ai_security_status_t* ai, const antivirus_status_t* av) {
    u32 i = g.hist_idx;
    g.hist[i].cpu = rs->cpu_busy_percent;
    g.hist[i].mem = rs->mem_total_bytes ? ((rs->mem_used_bytes * 100u) / rs->mem_total_bytes) : 0u;
    g.hist[i].io = clamp100((rs->io_queue_depth * 4u) + (rs->swap_used_kb / 64u));
    g.hist[i].net = clamp100(rs->net_packets / 8u);
    g.hist[i].thermal = re->thermal_celsius;
    g.hist[i].sec = clamp100((ai->risk_score / 4u) + (av->active_threats * 9u));
    g.hist[i].tick = timer_ticks();
    g.hist_idx = (i + 1u) % QS_HIST;
}

static void init_tasks(void) {
    for (u32 i = 0; i < QS_TASKS; i++) {
        g.tasks[i].id = i;
        g.tasks[i].class_hint = i % 3u;
        g.tasks[i].runtime = 0u;
        g.tasks[i].wakeups = 0u;
        g.tasks[i].blocking = 0u;
        for (u32 c = 0; c < QS_CORES; c++) {
            g.tasks[i].affinity[c] = 0u;
        }
        g.tasks[i].locality = (i * 13u) % 100u;
        g.tasks[i].criticality = 20u + (i * 7u) % 80u;
        g.tasks[i].risk = (i * 5u) % 40u;
        g.tasks[i].starvation = 0u;
        g.tasks[i].score = 0u;
    }
}

static void init_cores(void) {
    for (u32 i = 0; i < QS_CORES; i++) {
        g.cores[i].id = i;
        g.cores[i].load = 0u;
        g.cores[i].thermal = 40u;
        g.cores[i].idle = 100u;
        g.cores[i].queue = 0u;
        g.cores[i].cache_locality = 50u;
        g.cores[i].energy_cost = 20u + i * 3u;
        g.cores[i].score = 50u;
    }
}

static void init_graph(void) {
    for (u32 n = 0; n < QS_GRAPH; n++) {
        g.graph.edge_count[n] = 0u;
        g.graph.active[n] = 0u;
        for (u32 e = 0; e < 4u; e++) {
            g.graph.edges[n][e] = n;
        }
    }
    g.graph.depth = 1u;
    g.graph.breadth = 1u;
    g.graph.entropy = 0u;
}

static void init_risk(void) {
    for (u32 i = 0; i < QS_WIN; i++) g.risk.risk_window[i] = 0u;
    g.risk.idx = 0u;
    g.risk.momentum = 0u;
    g.risk.anomaly = 0u;
    g.risk.stability = 100u;
}

static void risk_update(void) {
    u32 cpu = avg_field_cpu(6u);
    u32 io = avg_field_io(6u);
    u32 sec = avg_field_sec(6u);
    u32 trend = slope_cpu(8u) + slope_io(8u);

    u32 risk = clamp100((cpu * 3u + io * 3u + sec * 3u + trend) / 10u);
    g.risk.risk_window[g.risk.idx] = risk;
    g.risk.idx = (g.risk.idx + 1u) % QS_WIN;

    u32 sum = 0u;
    for (u32 i = 0; i < QS_WIN; i++) sum += g.risk.risk_window[i];
    g.risk.momentum = sum / QS_WIN;

    g.risk.anomaly = clamp100((trend * 4u + g.risk.momentum * 6u) / 10u);
    g.risk.stability = 100u - g.risk.anomaly;
}

static u32 predict_pressure(void) {
    u32 cpu = avg_field_cpu(8u);
    u32 mem = avg_field_mem(8u);
    u32 io = avg_field_io(8u);
    u32 sec = avg_field_sec(8u);
    u32 trend = slope_cpu(8u) + slope_io(8u);

    u32 p = clamp100((cpu * 3u + mem * 3u + io * 2u + sec + trend) / 10u);
    return p;
}

static void update_core_model(const resource_snapshot_t* rs, const resource_engine_snapshot_t* re, const hal_status_t* hs) {
    u32 cores = core_cap(g.total_cores);
    for (u32 c = 0; c < cores; c++) {
        qs_core_t* k = &g.cores[c];
        u32 load = (rs->cpu_busy_percent + c * 7u + g.tick) % 100u;
        u32 thermal = min_u32(100u, re->thermal_celsius + c * 2u);
        u32 queue = (rs->io_queue_depth + c * 3u + rs->net_packets / 16u) % 100u;

        k->load = load;
        k->thermal = thermal;
        k->idle = 100u - load;
        k->queue = queue;
        k->cache_locality = clamp100((hs->hetero_core_score + c * 5u + 30u) % 100u);
        k->energy_cost = clamp100((thermal / 2u) + c * 2u);

        u32 score = 100u;
        if (score > load / 2u) score -= load / 2u; else score = 0u;
        if (score > thermal / 3u) score -= thermal / 3u; else score = 0u;
        score = clamp100(score + k->cache_locality / 4u);
        k->score = score;
    }
}

static void update_task_model(const resource_snapshot_t* rs, const hal_status_t* hs) {
    u32 cores = core_cap(g.total_cores);
    for (u32 t = 0; t < QS_TASKS; t++) {
        qs_task_t* q = &g.tasks[t];
        q->runtime += (t % 5u) + 1u;
        q->wakeups += (t % 3u);
        q->blocking = (q->blocking * 7u + rs->io_queue_depth) / 8u;

        u32 affinity_best = 0u;
        for (u32 c = 0; c < cores; c++) {
            u32 local = (q->locality + g.cores[c].cache_locality) / 2u;
            u32 thermal_pen = g.cores[c].thermal / 4u;
            u32 val = (local > thermal_pen) ? (local - thermal_pen) : 0u;
            q->affinity[c] = val;
            if (val > affinity_best) affinity_best = val;
        }

        q->risk = clamp100((q->risk * 7u + g.risk.momentum * 3u) / 10u);
        q->starvation = clamp100((q->starvation + (q->blocking > 50u ? 2u : 1u)));

        u32 class_boost = (q->class_hint == 0u) ? (hs->predicted_threads * 2u) :
                          ((q->class_hint == 1u) ? (rs->io_queue_depth * 2u) : (rs->net_packets / 8u));

        q->score = clamp100((affinity_best * 4u + q->criticality * 3u + class_boost * 2u + (100u - q->risk)) / 10u);
    }
}

static void graph_build(u32 pressure) {
    for (u32 n = 0; n < QS_GRAPH; n++) {
        u32 deg = 1u + ((n + pressure + g.tick) % 4u);
        g.graph.edge_count[n] = deg;
        g.graph.active[n] = ((n * 9u + pressure + g.tick) % 100u);
        for (u32 e = 0; e < deg; e++) {
            g.graph.edges[n][e] = (n + e + 1u + pressure % 7u) % QS_GRAPH;
        }
    }

    u32 active = 0u;
    u32 entropy = 0u;
    for (u32 n = 0; n < QS_GRAPH; n++) {
        if (g.graph.active[n] > 55u) active++;
        entropy += g.graph.edge_count[n] * g.graph.active[n];
    }

    g.graph.depth = 1u + active / 18u;
    if (g.graph.depth > 6u) g.graph.depth = 6u;

    g.graph.breadth = 1u + active / 24u;
    if (g.graph.breadth > 4u) g.graph.breadth = 4u;

    g.graph.entropy = clamp100(entropy / (QS_GRAPH * 4u));
}

static u32 pick_quantum_level(u32 mode, u32 pressure, u32 anomaly, u32 sec) {
    u32 idx = g.tick % 128u;
    u32 q = qs_mode_quantum_bias[(mode > 3u) ? 0u : mode][idx];

    if (pressure > 80u || anomaly > 75u || sec > 70u) return 1u;
    if (pressure > 55u || anomaly > 45u || sec > 45u) return (q < 2u) ? 2u : q;
    return (q > 3u) ? 3u : q;
}

static u32 pick_preempt_hint(u32 pressure, u32 anomaly) {
    if (pressure > 78u || anomaly > 70u) return 3u;
    if (pressure > 50u || anomaly > 42u) return 2u;
    return 1u;
}

static u32 predict_threads(u32 pressure, u32 mode, const hal_status_t* hs) {
    u32 cores = core_cap(g.total_cores);
    u32 t = 1u;

    if (mode == 2u || mode == 3u) {
        if (pressure > 75u) t = cores;
        else if (pressure > 55u) t = max_u32(2u, cores - 1u);
        else t = max_u32(2u, cores / 2u);
    } else {
        if (pressure > 75u) t = max_u32(2u, cores - 1u);
        else if (pressure > 55u) t = max_u32(1u, cores / 2u);
        else t = 1u;
    }

    if (hs->predicted_threads > t) t = min_u32(hs->predicted_threads, cores);
    if (t == 0u) t = 1u;
    return t;
}

static u32 task_fairness(void) {
    u32 sum = 0u;
    u32 minv = 100u;
    u32 maxv = 0u;
    for (u32 i = 0; i < QS_TASKS; i++) {
        u32 s = g.tasks[i].score;
        sum += s;
        if (s < minv) minv = s;
        if (s > maxv) maxv = s;
    }
    u32 avg = sum / QS_TASKS;
    u32 spread = maxv - minv;
    if (avg > spread / 2u) return clamp100(avg - spread / 2u);
    return 0u;
}

static u32 throughput_gain(void) {
    u32 core_sum = 0u;
    u32 task_sum = 0u;
    u32 cores = core_cap(g.total_cores);
    for (u32 c = 0; c < cores; c++) core_sum += g.cores[c].score;
    for (u32 t = 0; t < QS_TASKS; t++) task_sum += g.tasks[t].score;

    u32 core_avg = core_sum / cores;
    u32 task_avg = task_sum / QS_TASKS;
    return clamp100((core_avg * 6u + task_avg * 4u) / 10u);
}

static u32 compute_score(u32 pressure, u32 q, u32 preempt) {
    u32 fair = task_fairness();
    u32 gain = throughput_gain();
    u32 stable = g.risk.stability;

    u32 s = clamp100((gain * 4u + fair * 3u + stable * 2u + (100u - pressure)) / 10u);
    if (q == 1u) {
        s = clamp100((s * 9u) / 10u + preempt * 4u);
    }
    return s;
}

static u32 dependency_depth(void) {
    return g.graph.depth;
}

static u32 clone_level(u32 pressure, u32 mode) {
    if (mode == 2u || mode == 3u) {
        u32 c = 1u + pressure / 25u;
        if (c > 5u) c = 5u;
        return c;
    }
    u32 c = pressure / 35u;
    if (c > 3u) c = 3u;
    return c;
}

static u32 micro_thread_level(u32 pressure, u32 entropy) {
    u32 m = 1u + pressure / 35u + entropy / 45u;
    if (m > 4u) m = 4u;
    return m;
}

static u32 risk_index(const ai_security_status_t* ai, const antivirus_status_t* av) {
    return clamp100((ai->risk_score / 4u) + (av->active_threats * 8u) + g.risk.anomaly / 2u);
}

static u32 starvation_relief_score(void) {
    u32 relief = 0u;
    for (u32 i = 0; i < QS_TASKS; i++) {
        qs_task_t* t = &g.tasks[i];
        if (t->starvation > 40u) {
            u32 dec = t->starvation / 5u;
            if (t->score + dec > 100u) t->score = 100u;
            else t->score += dec;
            relief += dec;
            t->starvation = (t->starvation > 8u) ? (t->starvation - 8u) : 0u;
        }
    }
    return clamp100(relief / 2u);
}

static u32 thermal_fairness_guard(const resource_engine_snapshot_t* re) {
    u32 cores = core_cap(g.total_cores);
    u32 guard_boost = 0u;
    for (u32 c = 0; c < cores; c++) {
        qs_core_t* k = &g.cores[c];
        if (re->thermal_celsius > 85u && k->thermal > 80u) {
            if (k->score > 10u) k->score -= 10u;
            else k->score = 0u;
            guard_boost += 2u;
        } else if (k->idle > 60u) {
            k->score = clamp100(k->score + 4u);
            guard_boost += 1u;
        }
    }
    return clamp100(guard_boost * 4u);
}

static u32 dependency_pressure_index(void) {
    u32 idx = 0u;
    for (u32 n = 0; n < QS_GRAPH; n++) {
        idx += g.graph.edge_count[n] * (g.graph.active[n] / 10u + 1u);
    }
    idx /= QS_GRAPH;
    return clamp100(idx);
}

static u32 qos_latency_budget(u32 pressure, u32 risk) {
    u32 budget = 100u;
    if (pressure > 70u) {
        budget = (budget > 30u) ? (budget - 30u) : 0u;
    } else if (pressure > 50u) {
        budget = (budget > 15u) ? (budget - 15u) : 0u;
    }
    if (risk > 60u) {
        budget = (budget > 20u) ? (budget - 20u) : 0u;
    }
    return budget;
}

static u32 compute_extended_score(u32 base_score, u32 relief, u32 thermal_guard, u32 dep_idx, u32 qos_budget) {
    u32 s = base_score;
    s = clamp100((s * 6u + relief * 2u + thermal_guard + qos_budget / 2u) / 10u);
    if (dep_idx > 70u) {
        s = clamp100((s * 8u + 20u) / 10u);
    }
    return s;
}

void quantum_scheduler_init(u32 total_cores) {
    g.total_cores = core_cap(total_cores);
    g.tick = 0u;
    g.mode = 0u;
    g.hist_idx = 0u;

    for (u32 i = 0; i < QS_HIST; i++) {
        g.hist[i].cpu = 0u;
        g.hist[i].mem = 0u;
        g.hist[i].io = 0u;
        g.hist[i].net = 0u;
        g.hist[i].thermal = 0u;
        g.hist[i].sec = 0u;
        g.hist[i].tick = 0u;
    }

    init_tasks();
    init_cores();
    init_graph();
    init_risk();

    g.st.score = 0u;
    g.st.predicted_pressure = 0u;
    g.st.preempt_hint = 1u;
    g.st.quantum_level = 2u;
    g.st.dependency_depth = 1u;
    g.st.clone_level = 0u;
    g.st.micro_thread_level = 1u;
    g.st.predicted_threads = 1u;
    g.st.risk_index = 0u;
    g.st.stability = 100u;
    g.st.anomaly = 0u;
    g.st.throughput_gain = 0u;
    g.st.fairness_index = 100u;
}

void quantum_scheduler_tick(const resource_snapshot_t* rs,
                            const resource_engine_snapshot_t* re,
                            const hal_status_t* hs,
                            const ai_security_status_t* ai,
                            const antivirus_status_t* av,
                            u32 mode) {
    if (!rs || !re || !hs || !ai || !av) return;

    g.tick = timer_ticks();
    g.mode = mode;

    push_sample(rs, re, ai, av);
    risk_update();

    u32 pressure = predict_pressure();

    update_core_model(rs, re, hs);
    update_task_model(rs, hs);
    graph_build(pressure);

    u32 sec = risk_index(ai, av);
    u32 qlvl = pick_quantum_level(mode, pressure, g.risk.anomaly, sec);
    u32 preempt = pick_preempt_hint(pressure, g.risk.anomaly);

    u32 depth = dependency_depth();
    u32 clone = clone_level(pressure, mode);
    u32 micro = micro_thread_level(pressure, g.graph.entropy);
    u32 threads = predict_threads(pressure, mode, hs);

    u32 score = compute_score(pressure, qlvl, preempt);
    u32 relief = starvation_relief_score();
    u32 thermal_guard = thermal_fairness_guard(re);
    u32 dep_idx = dependency_pressure_index();
    u32 qos_budget = qos_latency_budget(pressure, sec);
    score = compute_extended_score(score, relief, thermal_guard, dep_idx, qos_budget);

    g.st.predicted_pressure = pressure;
    g.st.preempt_hint = preempt;
    g.st.quantum_level = qlvl;
    g.st.dependency_depth = depth;
    g.st.clone_level = clone;
    g.st.micro_thread_level = micro;
    g.st.predicted_threads = threads;
    g.st.risk_index = sec;
    g.st.stability = g.risk.stability;
    g.st.anomaly = g.risk.anomaly;
    g.st.throughput_gain = throughput_gain();
    g.st.fairness_index = task_fairness();
    g.st.score = score;
}

u32 quantum_scheduler_score(void) {
    return g.st.score;
}

u32 quantum_scheduler_preempt_hint(void) {
    return g.st.preempt_hint;
}

u32 quantum_scheduler_quantum_level(void) {
    return g.st.quantum_level;
}

u32 quantum_scheduler_dependency_depth(void) {
    return g.st.dependency_depth;
}

u32 quantum_scheduler_clone_level(void) {
    return g.st.clone_level;
}

u32 quantum_scheduler_micro_thread_level(void) {
    return g.st.micro_thread_level;
}

u32 quantum_scheduler_predicted_threads(void) {
    return g.st.predicted_threads;
}

u32 quantum_scheduler_anomaly(void) {
    return g.st.anomaly;
}

u32 quantum_scheduler_stability(void) {
    return g.st.stability;
}

void quantum_scheduler_status(quantum_scheduler_status_t* out) {
    if (!out) return;
    *out = g.st;
}
