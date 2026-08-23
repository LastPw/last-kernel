#include "pcoe2_core.h"

#define CORE_RING 64u

typedef struct {
    u32 cpu;
    u32 mem;
    u32 ioq;
    u32 net;
    u32 thermal;
} core_sample_t;

static core_sample_t g_ring[CORE_RING];
static u32 g_head = 0;
static u32 g_count = 0;

static pcoe2_core_view_t g_view;

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 abs_diff(u32 a, u32 b) {
    return (a > b) ? (a - b) : (b - a);
}

static u32 ewma_step(u32 prev, u32 cur, u32 alpha_num, u32 alpha_den) {
    return ((prev * (alpha_den - alpha_num)) + (cur * alpha_num)) / alpha_den;
}

static u32 avg_recent_cpu(u32 n) {
    if (g_count == 0u) return 0u;
    if (n > g_count) n = g_count;
    u32 sum = 0;
    for (u32 i = 0; i < n; i++) {
        u32 idx = (g_head + CORE_RING - 1u - i) % CORE_RING;
        sum += g_ring[idx].cpu;
    }
    return sum / n;
}

static u32 avg_recent_mem(u32 n) {
    if (g_count == 0u) return 0u;
    if (n > g_count) n = g_count;
    u32 sum = 0;
    for (u32 i = 0; i < n; i++) {
        u32 idx = (g_head + CORE_RING - 1u - i) % CORE_RING;
        sum += g_ring[idx].mem;
    }
    return sum / n;
}

static u32 avg_recent_ioq(u32 n) {
    if (g_count == 0u) return 0u;
    if (n > g_count) n = g_count;
    u32 sum = 0;
    for (u32 i = 0; i < n; i++) {
        u32 idx = (g_head + CORE_RING - 1u - i) % CORE_RING;
        sum += g_ring[idx].ioq;
    }
    return sum / n;
}

static u32 avg_recent_net(u32 n) {
    if (g_count == 0u) return 0u;
    if (n > g_count) n = g_count;
    u32 sum = 0;
    for (u32 i = 0; i < n; i++) {
        u32 idx = (g_head + CORE_RING - 1u - i) % CORE_RING;
        sum += g_ring[idx].net;
    }
    return sum / n;
}

void pcoe2_core_init(void) {
    for (u32 i = 0; i < CORE_RING; i++) {
        g_ring[i].cpu = 0;
        g_ring[i].mem = 0;
        g_ring[i].ioq = 0;
        g_ring[i].net = 0;
        g_ring[i].thermal = 0;
    }
    g_head = 0;
    g_count = 0;

    g_view.ewma_cpu = 0;
    g_view.ewma_mem = 0;
    g_view.ewma_ioq = 0;
    g_view.ewma_net = 0;
    g_view.ewma_thermal = 0;
    g_view.burst_cpu = 0;
    g_view.burst_io = 0;
    g_view.burst_net = 0;
    g_view.anomaly_score = 0;
    g_view.stability_score = 100;
    g_view.predicted_cpu = 0;
    g_view.predicted_mem = 0;
    g_view.predicted_ioq = 0;
    g_view.predicted_net = 0;
}

void pcoe2_core_ingest(const resource_snapshot_t* rs) {
    if (!rs) return;

    u32 memp = 0;
    if (rs->mem_total_bytes) {
        memp = (rs->mem_used_bytes * 100u) / rs->mem_total_bytes;
    }
    u32 ioq = clamp100(rs->io_queue_depth * 4u);
    u32 net = clamp100(rs->net_packets / 6u);

    core_sample_t s;
    s.cpu = clamp100(rs->cpu_busy_percent);
    s.mem = clamp100(memp);
    s.ioq = clamp100(ioq);
    s.net = clamp100(net);
    s.thermal = clamp100(rs->thermal_celsius);

    g_ring[g_head] = s;
    g_head = (g_head + 1u) % CORE_RING;
    if (g_count < CORE_RING) g_count++;

    g_view.ewma_cpu = ewma_step(g_view.ewma_cpu, s.cpu, 2u, 10u);
    g_view.ewma_mem = ewma_step(g_view.ewma_mem, s.mem, 2u, 10u);
    g_view.ewma_ioq = ewma_step(g_view.ewma_ioq, s.ioq, 3u, 10u);
    g_view.ewma_net = ewma_step(g_view.ewma_net, s.net, 3u, 10u);
    g_view.ewma_thermal = ewma_step(g_view.ewma_thermal, s.thermal, 2u, 10u);

    g_view.burst_cpu = abs_diff(s.cpu, g_view.ewma_cpu);
    g_view.burst_io = abs_diff(s.ioq, g_view.ewma_ioq);
    g_view.burst_net = abs_diff(s.net, g_view.ewma_net);

    g_view.anomaly_score = clamp100(
        (g_view.burst_cpu / 2u) +
        (g_view.burst_io / 2u) +
        (g_view.burst_net / 2u) +
        ((g_view.ewma_thermal > 85u) ? (g_view.ewma_thermal - 85u) : 0u));

    u32 volatility = clamp100((g_view.burst_cpu + g_view.burst_io + g_view.burst_net) / 3u);
    g_view.stability_score = (volatility > 100u) ? 0u : (100u - volatility);

    /* simple horizon: blend short and long windows */
    u32 c4 = avg_recent_cpu(4u);
    u32 c12 = avg_recent_cpu(12u);
    u32 m4 = avg_recent_mem(4u);
    u32 m12 = avg_recent_mem(12u);
    u32 i4 = avg_recent_ioq(4u);
    u32 i12 = avg_recent_ioq(12u);
    u32 n4 = avg_recent_net(4u);
    u32 n12 = avg_recent_net(12u);

    g_view.predicted_cpu = clamp100((c4 * 6u + c12 * 4u) / 10u + (g_view.burst_cpu / 4u));
    g_view.predicted_mem = clamp100((m4 * 6u + m12 * 4u) / 10u + (g_view.burst_cpu / 5u));
    g_view.predicted_ioq = clamp100((i4 * 7u + i12 * 3u) / 10u + (g_view.burst_io / 3u));
    g_view.predicted_net = clamp100((n4 * 7u + n12 * 3u) / 10u + (g_view.burst_net / 3u));
}

void pcoe2_core_view(pcoe2_core_view_t* out) {
    if (!out) return;
    *out = g_view;
}

u32 pcoe2_core_predict_pressure(void) {
    u32 p = (g_view.predicted_cpu + g_view.predicted_mem + g_view.predicted_ioq + g_view.predicted_net) / 4u;
    p += g_view.anomaly_score / 5u;
    return clamp100(p);
}

u32 pcoe2_core_predict_latency_shadow(void) {
    if (g_view.predicted_net > 70u || g_view.burst_net > 35u) return 3u;
    if (g_view.predicted_net > 35u || g_view.burst_net > 15u) return 2u;
    return 1u;
}

u32 pcoe2_core_recommended_quantum(void) {
    u32 pressure = pcoe2_core_predict_pressure();
    if (pressure > 75u || g_view.anomaly_score > 65u) return 1u;
    if (pressure > 45u) return 2u;
    return 3u;
}

u32 pcoe2_core_recommended_threads(u32 fallback_threads, u32 max_threads) {
    if (max_threads == 0u) max_threads = 1u;
    u32 pressure = pcoe2_core_predict_pressure();
    u32 t = fallback_threads ? fallback_threads : 1u;
    if (pressure > 75u) t += 2u;
    else if (pressure > 45u) t += 1u;
    else if (t > 1u) t -= 1u;
    if (t < 1u) t = 1u;
    if (t > max_threads) t = max_threads;
    return t;
}

