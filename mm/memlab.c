#include "memlab.h"
#include "heap.h"
#include "frame_allocator.h"
#include "slab.h"
#include "advanced_allocator.h"
#include "timer.h"

#define MEMLAB_RING 256u
#define MEMLAB_HIST_BINS 32u
#define MEMLAB_MAX_TMP_PTRS 128u

typedef struct {
    u32 tick;
    u32 heap_used;
    u32 heap_frag;
    u32 frame_used;
    u32 frame_total;
    u32 frame_frag;
    u32 frame_largest_order;
    u32 slab_total;
    u32 slab_free;
    u32 alloc_failures_total;
} memlab_sample_t;

typedef struct {
    memlab_sample_t ring[MEMLAB_RING];
    u32 head;
    u32 count;
    u32 hist_frag[MEMLAB_HIST_BINS];
    u32 hist_heap_used[MEMLAB_HIST_BINS];
    u32 alloc_failures_prev;
    u32 alloc_failures_delta;
    u32 frag_peak;
    u32 pressure;
    u32 risk;
    u32 health;
} memlab_state_t;

static memlab_state_t g;

static u32 clamp_u32(u32 v, u32 lo, u32 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static u32 bin_for_percent(u32 pct) {
    if (pct >= 100u) return MEMLAB_HIST_BINS - 1u;
    return (pct * MEMLAB_HIST_BINS) / 100u;
}

static u32 bin_for_heap_used(u32 used, u32 total) {
    if (total == 0u) return 0u;
    u32 pct = (used * 100u) / total;
    return bin_for_percent(clamp_u32(pct, 0u, 100u));
}

static u32 avg_recent_u32(u32 (*selector)(const memlab_sample_t*), u32 max_n) {
    if (g.count == 0u) return 0u;

    u32 n = g.count;
    if (n > max_n) n = max_n;
    if (n == 0u) return 0u;

    u32 sum = 0u;
    for (u32 i = 0u; i < n; i++) {
        u32 idx = (g.head + MEMLAB_RING - 1u - i) % MEMLAB_RING;
        sum += selector(&g.ring[idx]);
    }
    return sum / n;
}

static u32 sel_heap_used(const memlab_sample_t* s) { return s->heap_used; }
static u32 sel_heap_frag(const memlab_sample_t* s) { return s->heap_frag; }
static u32 sel_frame_used(const memlab_sample_t* s) { return s->frame_used; }
static u32 sel_frame_frag(const memlab_sample_t* s) { return s->frame_frag; }

static void slab_totals(u32* total, u32* freev) {
    u32 t = 0u;
    u32 f = 0u;
    u32 cnt = slab_class_count();
    for (u32 i = 0u; i < cnt; i++) {
        slab_class_stats_t st;
        if (slab_class_stats(i, &st) != 0) continue;
        t += st.total_objects;
        f += st.free_objects;
    }
    if (total) *total = t;
    if (freev) *freev = f;
}

static void recompute_scores(const memlab_sample_t* s) {
    u32 frame_free = 0u;
    if (s->frame_total > s->frame_used) {
        frame_free = s->frame_total - s->frame_used;
    }
    u32 frame_free_pct = 0u;
    if (s->frame_total > 0u) {
        frame_free_pct = (frame_free * 100u) / s->frame_total;
    }

    u32 pressure = 0u;
    pressure += s->heap_frag / 2u;
    pressure += s->frame_frag / 2u;
    pressure += (s->frame_largest_order < 2u) ? 20u : 0u;
    pressure += (frame_free_pct < 10u) ? 20u : 0u;
    pressure += (g.alloc_failures_delta > 0u) ? (10u + (g.alloc_failures_delta * 2u)) : 0u;
    pressure = clamp_u32(pressure, 0u, 100u);

    u32 risk = pressure;
    if (s->heap_frag > 60u && s->frame_frag > 60u) risk += 10u;
    if (s->frame_largest_order == 0u) risk += 10u;
    risk = clamp_u32(risk, 0u, 100u);

    g.pressure = pressure;
    g.risk = risk;
    g.health = 100u - risk;
}

void memlab_init(void) {
    g.head = 0u;
    g.count = 0u;
    g.alloc_failures_prev = adv_alloc_failures();
    g.alloc_failures_delta = 0u;
    g.frag_peak = 0u;
    g.pressure = 0u;
    g.risk = 0u;
    g.health = 100u;
    for (u32 i = 0u; i < MEMLAB_HIST_BINS; i++) {
        g.hist_frag[i] = 0u;
        g.hist_heap_used[i] = 0u;
    }
    for (u32 i = 0u; i < MEMLAB_RING; i++) {
        g.ring[i].tick = 0u;
        g.ring[i].heap_used = 0u;
        g.ring[i].heap_frag = 0u;
        g.ring[i].frame_used = 0u;
        g.ring[i].frame_total = 0u;
        g.ring[i].frame_frag = 0u;
        g.ring[i].frame_largest_order = 0u;
        g.ring[i].slab_total = 0u;
        g.ring[i].slab_free = 0u;
        g.ring[i].alloc_failures_total = 0u;
    }
}

void memlab_reset(void) {
    memlab_init();
}

void memlab_tick(void) {
    memlab_sample_t s;
    s.tick = timer_ticks();
    s.heap_used = heap_used_bytes();
    s.heap_frag = heap_fragmentation_score();
    s.frame_used = frame_used();
    s.frame_total = frame_total();
    s.frame_frag = frame_fragmentation_score();
    s.frame_largest_order = frame_largest_free_order();
    slab_totals(&s.slab_total, &s.slab_free);
    s.alloc_failures_total = adv_alloc_failures();

    if (s.alloc_failures_total >= g.alloc_failures_prev) {
        g.alloc_failures_delta = s.alloc_failures_total - g.alloc_failures_prev;
    } else {
        g.alloc_failures_delta = 0u;
    }
    g.alloc_failures_prev = s.alloc_failures_total;

    if (s.heap_frag > g.frag_peak) g.frag_peak = s.heap_frag;
    if (s.frame_frag > g.frag_peak) g.frag_peak = s.frame_frag;

    u32 frag_mix = (s.heap_frag + s.frame_frag) / 2u;
    u32 bfrag = bin_for_percent(frag_mix);
    u32 bheap = bin_for_heap_used(s.heap_used, s.frame_total * 4096u);
    if (bfrag < MEMLAB_HIST_BINS) g.hist_frag[bfrag]++;
    if (bheap < MEMLAB_HIST_BINS) g.hist_heap_used[bheap]++;

    g.ring[g.head] = s;
    g.head = (g.head + 1u) % MEMLAB_RING;
    if (g.count < MEMLAB_RING) g.count++;

    recompute_scores(&s);
}

static void fill_snapshot(memlab_snapshot_t* out) {
    if (!out) return;

    memlab_sample_t s;
    if (g.count == 0u) {
        s.tick = 0u;
        s.heap_used = heap_used_bytes();
        s.heap_frag = heap_fragmentation_score();
        s.frame_used = frame_used();
        s.frame_total = frame_total();
        s.frame_frag = frame_fragmentation_score();
        s.frame_largest_order = frame_largest_free_order();
        slab_totals(&s.slab_total, &s.slab_free);
        s.alloc_failures_total = adv_alloc_failures();
    } else {
        u32 idx = (g.head + MEMLAB_RING - 1u) % MEMLAB_RING;
        s = g.ring[idx];
    }

    out->samples_collected = g.count;
    out->sample_window = MEMLAB_RING;
    out->heap_used_avg = avg_recent_u32(sel_heap_used, 64u);
    out->heap_frag_avg = avg_recent_u32(sel_heap_frag, 64u);
    out->frame_used_avg = avg_recent_u32(sel_frame_used, 64u);
    out->frame_frag_avg = avg_recent_u32(sel_frame_frag, 64u);
    out->frame_largest_order = s.frame_largest_order;
    out->slab_total_objects = s.slab_total;
    out->slab_free_objects = s.slab_free;
    out->alloc_failures_total = s.alloc_failures_total;
    out->alloc_failures_delta = g.alloc_failures_delta;
    out->fragmentation_peak = g.frag_peak;
    out->pressure_score = g.pressure;
    out->risk_score = g.risk;
    out->health_score = g.health;
    out->last_tick = s.tick;
}

void memlab_snapshot(memlab_snapshot_t* out) {
    fill_snapshot(out);
}

int memlab_validate(memlab_validation_t* out) {
    memlab_validation_t r;
    r.checks_total = 0u;
    r.checks_passed = 0u;
    r.checks_failed = 0u;
    r.first_error = 0u;

    #define MEMLAB_CHECK(cond, err) do { \
        r.checks_total++; \
        if (cond) r.checks_passed++; \
        else { \
            r.checks_failed++; \
            if (r.first_error == 0u) r.first_error = (err); \
        } \
    } while (0)

    MEMLAB_CHECK(g.count <= MEMLAB_RING, 1001u);
    MEMLAB_CHECK(g.head < MEMLAB_RING, 1002u);
    MEMLAB_CHECK(heap_validate_integrity() == 0, 1003u);
    MEMLAB_CHECK(frame_used() <= frame_total(), 1004u);
    MEMLAB_CHECK(frame_fragmentation_score() <= 100u, 1005u);
    MEMLAB_CHECK(heap_fragmentation_score() <= 100u, 1006u);
    MEMLAB_CHECK(g.risk <= 100u, 1007u);
    MEMLAB_CHECK(g.health <= 100u, 1008u);

    u32 hist_sum = 0u;
    for (u32 i = 0u; i < MEMLAB_HIST_BINS; i++) {
        hist_sum += g.hist_frag[i];
    }
    MEMLAB_CHECK(hist_sum >= g.count || g.count == 0u, 1009u);

    if (g.count > 0u) {
        u32 prev = 0u;
        for (u32 i = 0u; i < g.count; i++) {
            u32 idx = (g.head + MEMLAB_RING - g.count + i) % MEMLAB_RING;
            u32 t = g.ring[idx].tick;
            MEMLAB_CHECK(t >= prev, 1010u);
            prev = t;
        }
    } else {
        MEMLAB_CHECK(1, 0u);
    }

    if (out) *out = r;
    return (r.checks_failed == 0u) ? 0 : -1;
}

static int scenario_heap_mix(u32 rounds, u32 payload, memlab_run_result_t* out) {
    void* ptrs[MEMLAB_MAX_TMP_PTRS];
    for (u32 i = 0u; i < MEMLAB_MAX_TMP_PTRS; i++) ptrs[i] = 0;

    u32 ops = 0u;
    u32 fail = 0u;
    u32 req = 0u;
    u32 got = 0u;
    u32 n = rounds;
    if (n < 16u) n = 16u;
    if (n > 8192u) n = 8192u;
    if (payload < 16u) payload = 16u;
    if (payload > 4096u) payload = 4096u;

    for (u32 i = 0u; i < n; i++) {
        u32 slot = i % MEMLAB_MAX_TMP_PTRS;
        if (ptrs[slot]) {
            adv_free(ptrs[slot]);
            ptrs[slot] = 0;
            ops++;
        }
        u32 sz = 16u + ((i * 37u + payload) % payload);
        req += sz;
        void* p = adv_alloc(sz);
        if (!p) {
            fail++;
            continue;
        }
        ptrs[slot] = p;
        got += sz;
        ops++;
        if ((i % 9u) == 0u) memlab_tick();
    }

    for (u32 i = 0u; i < MEMLAB_MAX_TMP_PTRS; i++) {
        if (ptrs[i]) {
            adv_free(ptrs[i]);
            ptrs[i] = 0;
            ops++;
        }
    }

    if (out) {
        out->operations += ops;
        out->failures += fail;
        out->bytes_requested += req;
        out->bytes_allocated += got;
    }
    return (fail == 0u) ? 0 : -1;
}

static int scenario_frame_order(u32 rounds, u32 payload, memlab_run_result_t* out) {
    (void)payload;
    void* blocks[64];
    u8 orders[64];
    for (u32 i = 0u; i < 64u; i++) {
        blocks[i] = 0;
        orders[i] = 0u;
    }

    u32 ops = 0u;
    u32 fail = 0u;
    u32 n = rounds;
    if (n < 32u) n = 32u;
    if (n > 4096u) n = 4096u;

    for (u32 i = 0u; i < n; i++) {
        u32 slot = i % 64u;
        if (blocks[slot]) {
            frame_free_order(blocks[slot], orders[slot]);
            blocks[slot] = 0;
            ops++;
        }
        u8 order = (u8)(i % 4u);
        void* b = frame_alloc_order(order);
        if (!b) {
            fail++;
            continue;
        }
        blocks[slot] = b;
        orders[slot] = order;
        ops++;
        if ((i % 11u) == 0u) memlab_tick();
    }

    for (u32 i = 0u; i < 64u; i++) {
        if (blocks[i]) {
            frame_free_order(blocks[i], orders[i]);
            ops++;
        }
    }

    if (out) {
        out->operations += ops;
        out->failures += fail;
    }
    return (fail == 0u) ? 0 : -1;
}

static int scenario_adv_pages(u32 rounds, u32 payload, memlab_run_result_t* out) {
    (void)payload;
    void* pages[32];
    for (u32 i = 0u; i < 32u; i++) {
        pages[i] = 0;
    }

    u32 ops = 0u;
    u32 fail = 0u;
    u32 req = 0u;
    u32 got = 0u;
    u32 n = rounds;
    if (n < 16u) n = 16u;
    if (n > 1024u) n = 1024u;

    for (u32 i = 0u; i < n; i++) {
        u32 slot = i % 32u;
        if (pages[slot]) {
            adv_free(pages[slot]);
            pages[slot] = 0;
            ops++;
        }

        u32 pc = 1u + (i % 6u);
        req += pc * 4096u;
        void* p = adv_alloc_pages(pc);
        if (!p) {
            fail++;
            continue;
        }
        pages[slot] = p;
        got += pc * 4096u;
        ops++;
        if ((i % 7u) == 0u) memlab_tick();
    }

    for (u32 i = 0u; i < 32u; i++) {
        if (pages[i]) {
            adv_free(pages[i]);
            ops++;
        }
    }

    if (out) {
        out->operations += ops;
        out->failures += fail;
        out->bytes_requested += req;
        out->bytes_allocated += got;
    }
    return (fail == 0u) ? 0 : -1;
}

static int scenario_slab_churn(u32 rounds, u32 payload, memlab_run_result_t* out) {
    void* ptrs[MEMLAB_MAX_TMP_PTRS];
    for (u32 i = 0u; i < MEMLAB_MAX_TMP_PTRS; i++) ptrs[i] = 0;

    u32 ops = 0u;
    u32 fail = 0u;
    u32 req = 0u;
    u32 got = 0u;
    u32 n = rounds;
    if (n < 64u) n = 64u;
    if (n > 8192u) n = 8192u;
    if (payload < 128u) payload = 128u;
    if (payload > 1024u) payload = 1024u;

    for (u32 i = 0u; i < n; i++) {
        u32 slot = (i * 3u) % MEMLAB_MAX_TMP_PTRS;
        if (ptrs[slot]) {
            adv_free(ptrs[slot]);
            ptrs[slot] = 0;
            ops++;
        }
        u32 sz = 8u + (i % payload);
        if (sz > 1024u) sz = 1024u;
        req += sz;
        void* p = adv_alloc(sz);
        if (!p) {
            fail++;
            continue;
        }
        ptrs[slot] = p;
        got += sz;
        ops++;
        if ((i % 13u) == 0u) memlab_tick();
    }

    for (u32 i = 0u; i < MEMLAB_MAX_TMP_PTRS; i++) {
        if (ptrs[i]) {
            adv_free(ptrs[i]);
            ops++;
        }
    }

    if (out) {
        out->operations += ops;
        out->failures += fail;
        out->bytes_requested += req;
        out->bytes_allocated += got;
    }
    return (fail == 0u) ? 0 : -1;
}

int memlab_run(u32 scenario, u32 rounds, u32 payload, memlab_run_result_t* out) {
    memlab_run_result_t r;
    r.scenario = scenario;
    r.rounds = rounds;
    r.operations = 0u;
    r.failures = 0u;
    r.bytes_requested = 0u;
    r.bytes_allocated = 0u;
    r.before_risk = g.risk;
    r.after_risk = g.risk;

    int rc = 0;
    switch (scenario) {
        case 0u: rc = scenario_heap_mix(rounds, payload, &r); break;
        case 1u: rc = scenario_frame_order(rounds, payload, &r); break;
        case 2u: rc = scenario_adv_pages(rounds, payload, &r); break;
        case 3u: rc = scenario_slab_churn(rounds, payload, &r); break;
        case 4u:
            rc = scenario_heap_mix(rounds / 2u + 1u, payload, &r);
            if (scenario_frame_order(rounds / 2u + 1u, payload, &r) != 0) rc = -1;
            if (scenario_adv_pages(rounds / 4u + 1u, payload, &r) != 0) rc = -1;
            break;
        default:
            rc = -2;
            break;
    }

    memlab_tick();
    r.after_risk = g.risk;
    if (out) *out = r;
    return rc;
}

u32 memlab_histogram_bin(u32 idx) {
    if (idx >= MEMLAB_HIST_BINS) return 0u;
    return g.hist_frag[idx];
}

u32 memlab_histogram_bins(void) {
    return MEMLAB_HIST_BINS;
}
