#include "compatlab.h"
#include "compat.h"
#include "timer.h"

#define CL_RING 256u

typedef struct {
    u32 tick;
    compat_status_t cs;
    u32 hit_ratio;
    u32 sync_pressure;
    u32 risk;
    u32 health;
    u32 mode_hint;
} cl_sample_t;

static cl_sample_t g_ring[CL_RING];
static u32 g_head = 0u;
static u32 g_count = 0u;
static u32 g_risk = 0u;
static u32 g_health = 100u;
static u32 g_mode_hint = 0u;

static u32 clamp100(u32 v) { return v > 100u ? 100u : v; }

void compatlab_init(void) {
    g_head = 0u;
    g_count = 0u;
    g_risk = 0u;
    g_health = 100u;
    g_mode_hint = 0u;
    for (u32 i = 0; i < CL_RING; i++) g_ring[i].tick = 0u;
}

void compatlab_reset(void) {
    compatlab_init();
}

void compatlab_tick(void) {
    cl_sample_t s;
    s.tick = timer_ticks();
    compat_status(&s.cs);
    u32 total_hits = s.cs.fast_path_hits + s.cs.slow_path_hits;
    s.hit_ratio = (total_hits == 0u) ? 100u : (s.cs.fast_path_hits * 100u) / total_hits;
    s.sync_pressure = clamp100((s.cs.dll_desync_events > 60u ? 60u : s.cs.dll_desync_events) +
                               (s.cs.abi_conflicts > 40u ? 40u : s.cs.abi_conflicts));
    u32 risk = 0u;
    if (s.hit_ratio < 60u) risk += (60u - s.hit_ratio) / 2u;
    risk += s.sync_pressure / 2u;
    if (s.cs.quarantined_profiles > 0u) risk += 20u;
    risk = clamp100(risk);
    s.risk = risk;
    s.health = 100u - risk;
    s.mode_hint = (risk > 70u) ? 2u : ((risk > 40u) ? 1u : 0u);

    g_risk = s.risk;
    g_health = s.health;
    g_mode_hint = s.mode_hint;
    g_ring[g_head] = s;
    g_head = (g_head + 1u) % CL_RING;
    if (g_count < CL_RING) g_count++;
}

void compatlab_snapshot(compatlab_snapshot_t* out) {
    if (!out) return;
    cl_sample_t s;
    if (g_count == 0u) {
        compat_status(&s.cs);
        s.tick = timer_ticks();
        u32 total = s.cs.fast_path_hits + s.cs.slow_path_hits;
        s.hit_ratio = (total == 0u) ? 100u : (s.cs.fast_path_hits * 100u) / total;
        s.sync_pressure = 0u;
        s.risk = g_risk;
        s.health = g_health;
        s.mode_hint = g_mode_hint;
    } else {
        s = g_ring[(g_head + CL_RING - 1u) % CL_RING];
    }
    out->samples = g_count;
    out->window = CL_RING;
    out->tracked_processes = s.cs.tracked_processes;
    out->fast_hits = s.cs.fast_path_hits;
    out->slow_hits = s.cs.slow_path_hits;
    out->hit_ratio_percent = s.hit_ratio;
    out->abi_conflicts = s.cs.abi_conflicts;
    out->cache_entries = s.cs.cache_entries;
    out->dll_sync_score = s.cs.dll_avg_sync_score;
    out->dll_desync_events = s.cs.dll_desync_events;
    out->sync_pressure = s.sync_pressure;
    out->risk_score = s.risk;
    out->health_score = s.health;
    out->mode_hint = s.mode_hint;
    out->last_tick = s.tick;
}

int compatlab_validate(compatlab_validation_t* out) {
    compatlab_validation_t r = {0,0,0,0};
    #define CLCHECK(cond, err) do { r.checks_total++; if (cond) r.checks_passed++; else { r.checks_failed++; if (!r.first_error) r.first_error = (err);} } while (0)
    CLCHECK(g_head < CL_RING, 5001u);
    CLCHECK(g_count <= CL_RING, 5002u);
    CLCHECK(g_risk <= 100u, 5003u);
    CLCHECK(g_health <= 100u, 5004u);
    CLCHECK(g_mode_hint <= 2u, 5005u);
    if (out) *out = r;
    return r.checks_failed ? -1 : 0;
}

int compatlab_run(u32 mode, u32 rounds, compatlab_run_result_t* out) {
    if (rounds < 16u) rounds = 16u;
    if (rounds > 2048u) rounds = 2048u;
    compat_status_t b, a;
    compat_status(&b);
    compatlab_run_result_t r;
    r.mode = mode;
    r.rounds = rounds;
    r.fast_before = b.fast_path_hits;
    r.fast_after = b.fast_path_hits;
    r.slow_before = b.slow_path_hits;
    r.slow_after = b.slow_path_hits;
    r.conflicts_before = b.abi_conflicts;
    r.conflicts_after = b.abi_conflicts;
    r.risk_before = g_risk;
    r.risk_after = g_risk;
    r.failures = 0u;

    for (u32 i = 0; i < rounds; i++) {
        u32 pid = 1u + (i % 4u);
        u32 ret = 0u;
        u32 nr = (mode == 0u) ? (i % 6u) : ((mode == 1u) ? (50u + (i % 8u)) : (200u + (i % 16u)));
        if (compat_syscall(pid, nr, i, i + 1u, i + 2u, &ret) != 0) {
            r.failures++;
        }
        if ((i % 5u) == 0u) {
            (void)compat_autosync_step(8u);
        }
        if ((i % 3u) == 0u) compatlab_tick();
    }
    compatlab_tick();
    compat_status(&a);
    r.fast_after = a.fast_path_hits;
    r.slow_after = a.slow_path_hits;
    r.conflicts_after = a.abi_conflicts;
    r.risk_after = g_risk;
    if (out) *out = r;
    return (r.failures == 0u) ? 0 : -1;
}
