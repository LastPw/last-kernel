#include "hyper_ghost.h"
#include "timer.h"

#define HG_HIST 96u
#define HG_TIERS 4u
#define HG_POOLS 8u
#define HG_REGIONS 64u
#define HG_WINDOWS 12u

typedef struct {
    u32 used_pct;
    u32 io_pressure;
    u32 net_pressure;
    u32 thermal;
    u32 predicted_alloc;
    u32 low_mem;
    u32 swap_kb;
} hg_sample_t;

typedef struct {
    u32 capacity_kb;
    u32 used_kb;
    u32 hits;
    u32 misses;
    u32 churn;
    u32 age;
} hg_pool_t;

typedef struct {
    u32 id;
    u32 hotness;
    u32 pages;
    u32 shadow_pages;
    u32 compressible;
    u32 dedupable;
    u32 fault_rate;
    u32 dirty_rate;
    u32 reclaim_cost;
    u32 pinned;
} hg_region_t;

typedef struct {
    u32 target_kb;
    u32 actual_kb;
    u32 pressure;
    u32 reserve_floor_kb;
    u32 reserve_ceiling_kb;
} hg_tier_t;

typedef struct {
    u32 risk_window[HG_WINDOWS];
    u32 idx;
    u32 momentum;
    u32 anomaly;
    u32 stability;
} hg_predictor_t;

typedef struct {
    u32 total_ram_kb;
    u32 ticks;
    u32 mode;

    hg_sample_t hist[HG_HIST];
    u32 hist_idx;

    hg_pool_t pools[HG_POOLS];
    hg_region_t regions[HG_REGIONS];
    hg_tier_t tiers[HG_TIERS];
    hg_predictor_t pred;

    hyper_ghost_status_t st;
} hg_state_t;

static hg_state_t g;

static const u16 hg_weight_mode0[128] = {
    8,9,10,11,12,13,14,15,9,10,11,12,13,14,15,16,
    10,11,12,13,14,15,16,17,11,12,13,14,15,16,17,18,
    9,10,11,12,13,14,15,16,8,9,10,11,12,13,14,15,
    7,8,9,10,11,12,13,14,6,7,8,9,10,11,12,13,
    5,6,7,8,9,10,11,12,4,5,6,7,8,9,10,11,
    6,7,8,9,10,11,12,13,7,8,9,10,11,12,13,14,
    8,9,10,11,12,13,14,15,9,10,11,12,13,14,15,16,
    10,11,12,13,14,15,16,17,11,12,13,14,15,16,17,18
};

static const u16 hg_weight_mode1[128] = {
    10,11,12,13,14,15,16,17,11,12,13,14,15,16,17,18,
    12,13,14,15,16,17,18,19,13,14,15,16,17,18,19,20,
    12,13,14,15,16,17,18,19,11,12,13,14,15,16,17,18,
    10,11,12,13,14,15,16,17,9,10,11,12,13,14,15,16,
    8,9,10,11,12,13,14,15,7,8,9,10,11,12,13,14,
    8,9,10,11,12,13,14,15,9,10,11,12,13,14,15,16,
    10,11,12,13,14,15,16,17,11,12,13,14,15,16,17,18,
    12,13,14,15,16,17,18,19,13,14,15,16,17,18,19,20
};

static const u16 hg_weight_mode2[128] = {
    14,15,16,17,18,19,20,21,15,16,17,18,19,20,21,22,
    16,17,18,19,20,21,22,23,17,18,19,20,21,22,23,24,
    16,17,18,19,20,21,22,23,15,16,17,18,19,20,21,22,
    14,15,16,17,18,19,20,21,13,14,15,16,17,18,19,20,
    12,13,14,15,16,17,18,19,11,12,13,14,15,16,17,18,
    12,13,14,15,16,17,18,19,13,14,15,16,17,18,19,20,
    14,15,16,17,18,19,20,21,15,16,17,18,19,20,21,22,
    16,17,18,19,20,21,22,23,17,18,19,20,21,22,23,24
};

static const u16 hg_weight_mode3[128] = {
    16,17,18,19,20,21,22,23,17,18,19,20,21,22,23,24,
    18,19,20,21,22,23,24,25,19,20,21,22,23,24,25,26,
    18,19,20,21,22,23,24,25,17,18,19,20,21,22,23,24,
    16,17,18,19,20,21,22,23,15,16,17,18,19,20,21,22,
    14,15,16,17,18,19,20,21,13,14,15,16,17,18,19,20,
    14,15,16,17,18,19,20,21,15,16,17,18,19,20,21,22,
    16,17,18,19,20,21,22,23,17,18,19,20,21,22,23,24,
    18,19,20,21,22,23,24,25,19,20,21,22,23,24,25,26
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

static const u16* mode_weights(u32 mode) {
    if (mode == 1u) return hg_weight_mode1;
    if (mode == 2u) return hg_weight_mode2;
    if (mode == 3u) return hg_weight_mode3;
    return hg_weight_mode0;
}

static u32 hist_at(u32 back) {
    if (back >= HG_HIST) back = HG_HIST - 1u;
    return (g.hist_idx + HG_HIST - back) % HG_HIST;
}

static u32 avg_used(u32 n) {
    if (n == 0u) return 0u;
    if (n > HG_HIST) n = HG_HIST;
    u32 sum = 0u;
    for (u32 i = 0; i < n; i++) {
        sum += g.hist[hist_at(i)].used_pct;
    }
    return sum / n;
}

static u32 avg_io(u32 n) {
    if (n == 0u) return 0u;
    if (n > HG_HIST) n = HG_HIST;
    u32 sum = 0u;
    for (u32 i = 0; i < n; i++) {
        sum += g.hist[hist_at(i)].io_pressure;
    }
    return sum / n;
}

static u32 avg_swap(u32 n) {
    if (n == 0u) return 0u;
    if (n > HG_HIST) n = HG_HIST;
    u32 sum = 0u;
    for (u32 i = 0; i < n; i++) {
        sum += g.hist[hist_at(i)].swap_kb;
    }
    return sum / n;
}

static u32 avg_thermal(u32 n) {
    if (n == 0u) return 0u;
    if (n > HG_HIST) n = HG_HIST;
    u32 sum = 0u;
    for (u32 i = 0; i < n; i++) {
        sum += g.hist[hist_at(i)].thermal;
    }
    return sum / n;
}

static u32 slope_used(u32 n) {
    if (n < 2u) return 0u;
    if (n > HG_HIST) n = HG_HIST;
    u32 newer = g.hist[hist_at(0u)].used_pct;
    u32 older = g.hist[hist_at(n - 1u)].used_pct;
    return (newer > older) ? (newer - older) : 0u;
}

static void push_sample(const resource_snapshot_t* rs, const resource_engine_snapshot_t* re) {
    u32 i = g.hist_idx;
    g.hist[i].used_pct = rs->mem_total_bytes ? ((rs->mem_used_bytes * 100u) / rs->mem_total_bytes) : 0u;
    g.hist[i].io_pressure = clamp100((rs->io_queue_depth * 4u) + (rs->swap_used_kb / 64u));
    g.hist[i].net_pressure = clamp100(rs->net_packets / 8u);
    g.hist[i].thermal = re->thermal_celsius;
    g.hist[i].predicted_alloc = rs->predicted_next_alloc;
    g.hist[i].low_mem = rs->low_mem_warning ? 1u : 0u;
    g.hist[i].swap_kb = rs->swap_used_kb;
    g.hist_idx = (i + 1u) % HG_HIST;
}

static void update_predictor(void) {
    u32 cpu = g.hist[hist_at(0u)].used_pct;
    u32 io = g.hist[hist_at(0u)].io_pressure;
    u32 thermal = g.hist[hist_at(0u)].thermal;
    u32 swap = g.hist[hist_at(0u)].swap_kb;

    u32 risk = clamp100((cpu * 4u + io * 3u + (thermal > 100u ? 100u : thermal) * 2u + (swap / 64u)) / 10u);

    g.pred.risk_window[g.pred.idx] = risk;
    g.pred.idx = (g.pred.idx + 1u) % HG_WINDOWS;

    u32 momentum = 0u;
    for (u32 i = 0; i < HG_WINDOWS; i++) momentum += g.pred.risk_window[i];
    g.pred.momentum = momentum / HG_WINDOWS;

    u32 trend = slope_used(8u);
    g.pred.anomaly = clamp100((trend * 3u + g.pred.momentum * 7u) / 10u);
    g.pred.stability = 100u - g.pred.anomaly;
}

static void init_pools(void) {
    u32 base = (g.total_ram_kb / 32u);
    for (u32 i = 0; i < HG_POOLS; i++) {
        g.pools[i].capacity_kb = base + (i * (base / 4u + 1u));
        g.pools[i].used_kb = 0u;
        g.pools[i].hits = 0u;
        g.pools[i].misses = 0u;
        g.pools[i].churn = 0u;
        g.pools[i].age = 0u;
    }
}

static void init_regions(void) {
    u32 pages_per = (g.total_ram_kb / 4u) / HG_REGIONS;
    for (u32 i = 0; i < HG_REGIONS; i++) {
        g.regions[i].id = i;
        g.regions[i].hotness = (i * 13u) % 100u;
        g.regions[i].pages = max_u32(8u, pages_per);
        g.regions[i].shadow_pages = g.regions[i].pages / 8u;
        g.regions[i].compressible = 50u + ((i * 3u) % 50u);
        g.regions[i].dedupable = 40u + ((i * 5u) % 60u);
        g.regions[i].fault_rate = (i * 7u) % 30u;
        g.regions[i].dirty_rate = (i * 11u) % 40u;
        g.regions[i].reclaim_cost = 10u + ((i * 9u) % 70u);
        g.regions[i].pinned = (i % 17u == 0u) ? 1u : 0u;
    }
}

static void init_tiers(void) {
    u32 quarter = g.total_ram_kb / 4u;
    for (u32 i = 0; i < HG_TIERS; i++) {
        g.tiers[i].target_kb = quarter;
        g.tiers[i].actual_kb = quarter;
        g.tiers[i].pressure = 0u;
        g.tiers[i].reserve_floor_kb = quarter / 8u;
        g.tiers[i].reserve_ceiling_kb = quarter + quarter / 4u;
    }
}

static void pool_assign(u32 idx, u32 demand_kb, u32 hot_factor) {
    hg_pool_t* p = &g.pools[idx];
    u32 grant = min_u32(demand_kb, p->capacity_kb);

    if (grant > p->used_kb) {
        u32 delta = grant - p->used_kb;
        p->used_kb += delta;
        p->hits += (hot_factor > 50u) ? 2u : 1u;
    } else {
        u32 delta = p->used_kb - grant;
        p->used_kb -= delta;
        p->churn += delta / 8u;
    }

    if (demand_kb > p->capacity_kb) {
        p->misses += (demand_kb - p->capacity_kb) / 16u + 1u;
    }

    p->age++;
}

static void update_pools(const resource_snapshot_t* rs) {
    u32 demand_base = rs->predicted_next_alloc / 1024u;
    u32 used_recent = avg_used(8u);
    u32 io_recent = avg_io(8u);

    for (u32 i = 0; i < HG_POOLS; i++) {
        u32 hot = (used_recent + io_recent + i * 7u) % 100u;
        u32 demand = demand_base + i * (2u + hot / 20u);
        if (rs->low_mem_warning && i < 3u) {
            demand = demand / 2u;
        }
        pool_assign(i, demand, hot);
    }
}

static void update_regions(const resource_snapshot_t* rs, const hal_status_t* hs) {
    u32 mem_p = rs->mem_total_bytes ? ((rs->mem_used_bytes * 100u) / rs->mem_total_bytes) : 0u;
    u32 bias = hs->data_prefetch_score;

    for (u32 i = 0; i < HG_REGIONS; i++) {
        hg_region_t* r = &g.regions[i];

        u32 old_hot = r->hotness;
        u32 access = (i * 17u + g.ticks + mem_p + bias) % 100u;

        if (access > old_hot) r->hotness += (access - old_hot) / 2u;
        else r->hotness -= (old_hot - access) / 3u;

        if (r->hotness > 100u) r->hotness = 100u;

        if (r->pinned) {
            r->shadow_pages = r->pages / 16u;
        } else if (r->hotness > 70u) {
            r->shadow_pages = r->pages / 4u;
        } else if (r->hotness > 40u) {
            r->shadow_pages = r->pages / 8u;
        } else {
            r->shadow_pages = r->pages / 12u;
        }

        r->fault_rate = clamp100((r->fault_rate * 7u + mem_p * 3u) / 10u);
        r->dirty_rate = clamp100((r->dirty_rate * 6u + rs->io_queue_depth * 2u + rs->swap_used_kb / 64u) / 10u);

        r->reclaim_cost = clamp100((r->hotness * 4u + r->dirty_rate * 4u + (100u - r->compressible) * 2u) / 10u);
    }
}

static void rebalance_tiers(const resource_snapshot_t* rs, const resource_engine_snapshot_t* re) {
    u32 used_pct = rs->mem_total_bytes ? ((rs->mem_used_bytes * 100u) / rs->mem_total_bytes) : 0u;
    u32 thermal = re->thermal_celsius;
    u32 pressure = clamp100((used_pct * 7u + rs->io_queue_depth * 3u) / 10u);

    for (u32 i = 0; i < HG_TIERS; i++) {
        hg_tier_t* t = &g.tiers[i];
        t->pressure = pressure + i * 5u;
        if (t->pressure > 100u) t->pressure = 100u;

        u32 target = t->target_kb;
        if (t->pressure > 70u) {
            target = (target * 9u) / 10u;
        } else {
            target = (target * 11u) / 10u;
        }

        if (thermal > 85u && i == 0u) {
            target = (target * 8u) / 10u;
        }

        if (target < t->reserve_floor_kb) target = t->reserve_floor_kb;
        if (target > t->reserve_ceiling_kb) target = t->reserve_ceiling_kb;

        if (target > t->actual_kb) t->actual_kb += (target - t->actual_kb) / 2u;
        else t->actual_kb -= (t->actual_kb - target) / 2u;
    }
}

static u32 pool_health(void) {
    u32 hit = 0u;
    u32 miss = 0u;
    u32 churn = 0u;

    for (u32 i = 0; i < HG_POOLS; i++) {
        hit += g.pools[i].hits;
        miss += g.pools[i].misses;
        churn += g.pools[i].churn;
    }

    u32 ratio = (hit + miss) ? (hit * 100u) / (hit + miss) : 100u;
    u32 churn_penalty = (churn > 500u) ? 25u : (churn / 20u);
    if (ratio > churn_penalty) ratio -= churn_penalty;
    else ratio = 0u;
    return clamp100(ratio);
}

static u32 region_shadow_pages(void) {
    u32 sum = 0u;
    for (u32 i = 0; i < HG_REGIONS; i++) {
        sum += g.regions[i].shadow_pages;
    }
    return sum;
}

static u32 compress_permille(void) {
    u32 weighted = 0u;
    for (u32 i = 0; i < HG_REGIONS; i++) {
        weighted += g.regions[i].compressible;
    }
    weighted /= HG_REGIONS;

    u32 tier_pressure = 0u;
    for (u32 i = 0; i < HG_TIERS; i++) tier_pressure += g.tiers[i].pressure;
    tier_pressure /= HG_TIERS;

    u32 c = 850u + weighted;
    if (tier_pressure > 70u) c += 50u;
    if (c > 1300u) c = 1300u;
    return c;
}

static u32 dedup_permille(void) {
    u32 weighted = 0u;
    for (u32 i = 0; i < HG_REGIONS; i++) {
        weighted += g.regions[i].dedupable;
    }
    weighted /= HG_REGIONS;

    u32 p = 820u + weighted;
    if (g.pred.momentum > 60u) p += 40u;
    if (p > 1250u) p = 1250u;
    return p;
}

static u32 calc_reserved_kb(u32 mode, const resource_snapshot_t* rs, const hal_status_t* hs) {
    const u16* w = mode_weights(mode);
    u32 slot = g.ticks % 128u;
    u32 base = (g.total_ram_kb / 16u);
    u32 bias = w[slot] * (g.total_ram_kb / 1024u) / 10u;
    u32 low_mem_penalty = rs->low_mem_warning ? (base / 2u) : 0u;
    u32 thermal_penalty = (rs->thermal_celsius > 80u) ? (base / 4u) : 0u;
    u32 prefetch_bonus = hs->data_prefetch_score * 32u;

    u32 reserve = base + bias + prefetch_bonus;
    if (reserve > low_mem_penalty + thermal_penalty) reserve -= (low_mem_penalty + thermal_penalty);
    else reserve = base / 2u;

    u32 max_reserve = g.total_ram_kb / 3u;
    if (reserve > max_reserve) reserve = max_reserve;
    if (reserve < g.total_ram_kb / 32u) reserve = g.total_ram_kb / 32u;
    return reserve;
}

static u32 calc_pressure(const resource_snapshot_t* rs) {
    u32 used_now = rs->mem_total_bytes ? ((rs->mem_used_bytes * 100u) / rs->mem_total_bytes) : 0u;
    u32 used_avg = avg_used(10u);
    u32 io_avg = avg_io(10u);
    u32 swp = avg_swap(10u) / 64u;
    u32 trend = slope_used(10u);

    u32 p = (used_now * 4u + used_avg * 2u + io_avg * 2u + swp + trend) / 10u;
    if (rs->low_mem_warning) p += 12u;
    if (p > 100u) p = 100u;
    return p;
}

static u32 calc_latency_shadow(u32 pressure, u32 thermal) {
    if (pressure > 80u || thermal > 90u) return 3u;
    if (pressure > 55u || thermal > 75u) return 2u;
    return 1u;
}

static void perform_reclaim_if_needed(const resource_snapshot_t* rs) {
    if (!rs->low_mem_warning && g.st.pressure < 85u) return;

    u32 reclaimed = 0u;
    for (u32 i = 0; i < HG_REGIONS; i++) {
        hg_region_t* r = &g.regions[i];
        if (r->pinned) continue;
        if (r->hotness > 60u) continue;
        u32 trim = r->shadow_pages / 3u;
        if (trim > 0u) {
            r->shadow_pages -= trim;
            reclaimed += trim;
        }
    }

    if (reclaimed > 0u) {
        g.st.reclaim_events++;
    }
}

static void rebalance_pools_if_needed(void) {
    u32 health = pool_health();
    if (health > 70u) return;

    for (u32 i = 0; i < HG_POOLS; i++) {
        hg_pool_t* p = &g.pools[i];
        if (p->used_kb > p->capacity_kb) {
            p->used_kb = p->capacity_kb;
            g.st.rebalance_events++;
        }
        if (p->misses > p->hits * 2u) {
            p->capacity_kb += p->capacity_kb / 10u;
            g.st.rebalance_events++;
        }
    }
}

static u32 calc_hot_pool_kb(void) {
    u32 s = 0u;
    for (u32 i = 0; i < HG_POOLS / 3u; i++) s += g.pools[i].used_kb;
    return s;
}

static u32 calc_warm_pool_kb(void) {
    u32 s = 0u;
    for (u32 i = HG_POOLS / 3u; i < (2u * HG_POOLS) / 3u; i++) s += g.pools[i].used_kb;
    return s;
}

static u32 calc_cold_pool_kb(void) {
    u32 s = 0u;
    for (u32 i = (2u * HG_POOLS) / 3u; i < HG_POOLS; i++) s += g.pools[i].used_kb;
    return s;
}

static u32 calc_score(const resource_snapshot_t* rs, const hal_status_t* hs) {
    u32 health = pool_health();
    u32 stable = g.pred.stability;
    u32 prefetch = hs->data_prefetch_score;
    u32 use = rs->mem_total_bytes ? ((rs->mem_used_bytes * 100u) / rs->mem_total_bytes) : 0u;

    u32 base = (health * 4u + stable * 3u + prefetch * 2u + (100u - use)) / 10u;
    if (g.st.pressure > 70u) {
        if (base > 15u) base -= 15u;
        else base = 0u;
    }
    return clamp100(base);
}

static void update_policy_mode(u32 mode, const resource_snapshot_t* rs, const resource_engine_snapshot_t* re) {
    if (mode == 3u || mode == 2u) {
        g.st.policy_mode = 2u;
        return;
    }
    if (rs->low_mem_warning || re->thermal_celsius > 88u) {
        g.st.policy_mode = 1u;
        return;
    }
    g.st.policy_mode = 0u;
}

void hyper_ghost_init(u32 total_ram_bytes) {
    g.total_ram_kb = total_ram_bytes / 1024u;
    if (g.total_ram_kb == 0u) g.total_ram_kb = 1024u;
    g.ticks = 0u;
    g.mode = 0u;
    g.hist_idx = 0u;

    for (u32 i = 0; i < HG_HIST; i++) {
        g.hist[i].used_pct = 0u;
        g.hist[i].io_pressure = 0u;
        g.hist[i].net_pressure = 0u;
        g.hist[i].thermal = 0u;
        g.hist[i].predicted_alloc = 0u;
        g.hist[i].low_mem = 0u;
        g.hist[i].swap_kb = 0u;
    }

    for (u32 i = 0; i < HG_WINDOWS; i++) g.pred.risk_window[i] = 0u;
    g.pred.idx = 0u;
    g.pred.momentum = 0u;
    g.pred.anomaly = 0u;
    g.pred.stability = 100u;

    init_pools();
    init_regions();
    init_tiers();

    g.st.score = 0u;
    g.st.pressure = 0u;
    g.st.latency_shadow = 1u;
    g.st.reserved_kb = g.total_ram_kb / 16u;
    g.st.hot_pool_kb = 0u;
    g.st.warm_pool_kb = 0u;
    g.st.cold_pool_kb = 0u;
    g.st.shadow_pages = 0u;
    g.st.compression_permille = 1000u;
    g.st.dedup_permille = 1000u;
    g.st.reclaim_events = 0u;
    g.st.rebalance_events = 0u;
    g.st.low_mem_events = 0u;
    g.st.policy_mode = 0u;
}

void hyper_ghost_tick(const resource_snapshot_t* rs,
                      const resource_engine_snapshot_t* re,
                      const hal_status_t* hs,
                      u32 mode) {
    if (!rs || !re || !hs) return;

    g.ticks = timer_ticks();
    g.mode = mode;

    push_sample(rs, re);
    update_predictor();

    if (rs->low_mem_warning) {
        g.st.low_mem_events++;
    }

    update_pools(rs);
    update_regions(rs, hs);
    rebalance_tiers(rs, re);

    g.st.pressure = calc_pressure(rs);
    g.st.latency_shadow = calc_latency_shadow(g.st.pressure, avg_thermal(8u));

    g.st.reserved_kb = calc_reserved_kb(mode, rs, hs);
    g.st.shadow_pages = region_shadow_pages();
    g.st.compression_permille = compress_permille();
    g.st.dedup_permille = dedup_permille();

    perform_reclaim_if_needed(rs);
    rebalance_pools_if_needed();

    g.st.hot_pool_kb = calc_hot_pool_kb();
    g.st.warm_pool_kb = calc_warm_pool_kb();
    g.st.cold_pool_kb = calc_cold_pool_kb();

    update_policy_mode(mode, rs, re);

    g.st.score = calc_score(rs, hs);
}

u32 hyper_ghost_score(void) {
    return g.st.score;
}

u32 hyper_ghost_pressure(void) {
    return g.st.pressure;
}

u32 hyper_ghost_latency_shadow(void) {
    return g.st.latency_shadow;
}

u32 hyper_ghost_reserved_kb(void) {
    return g.st.reserved_kb;
}

void hyper_ghost_status(hyper_ghost_status_t* out) {
    if (!out) return;
    *out = g.st;
}

static u32 s_frozen_task_mask = 0;
static u32 s_frozen_count = 0;

int hyper_ghost_freeze_task(u32 task_id) {
    if (task_id == 0 || task_id >= 32) return -1;
    s_frozen_task_mask |= (1u << task_id);
    s_frozen_count++;
    g.st.dedup_permille += 15;
    g.st.compression_permille += 30;
    return 0;
}

int hyper_ghost_thaw_task(u32 task_id) {
    if (task_id == 0 || task_id >= 32) return -1;
    if (s_frozen_task_mask & (1u << task_id)) {
        s_frozen_task_mask &= ~(1u << task_id);
        if (s_frozen_count > 0) s_frozen_count--;
    }
    return 0;
}

u32 hyper_ghost_frozen_task_count(void) {
    return s_frozen_count;
}
