#include "fusionlab.h"
#include "memlab.h"
#include "schedlab.h"
#include "iolab.h"
#include "seclab.h"
#include "driverlab.h"
#include "timer.h"

#define FL_RING 256u

typedef struct {
    u32 tick;
    u32 mem_h;
    u32 sch_h;
    u32 io_h;
    u32 sec_h;
    u32 cmp_h;
    u32 drv_h;
    u32 agg_h;
    u32 agg_r;
    u32 bottleneck;
    u32 mode_hint;
} fl_sample_t;

static fl_sample_t g_ring[FL_RING];
static u32 g_head = 0u, g_count = 0u;
static u32 g_agg_h = 100u, g_agg_r = 0u, g_bneck = 0u, g_mode = 0u;

void fusionlab_init(void) {
    g_head = 0u; g_count = 0u; g_agg_h = 100u; g_agg_r = 0u; g_bneck = 0u; g_mode = 0u;
    for (u32 i = 0; i < FL_RING; i++) g_ring[i].tick = 0u;
}

void fusionlab_reset(void) { fusionlab_init(); }

void fusionlab_tick(void) {
    memlab_snapshot_t m; schedlab_snapshot_t s; iolab_snapshot_t i; seclab_snapshot_t c;
    memlab_snapshot(&m); schedlab_snapshot(&s); iolab_snapshot(&i); seclab_snapshot(&c);

    fl_sample_t x;
    x.tick = timer_ticks();
    x.mem_h = m.health_score;
    x.sch_h = s.health_score;
    x.io_h = i.health_score;
    x.sec_h = c.health_score;
    x.cmp_h = 100u;
    x.drv_h = 100u;
    x.agg_h = (x.mem_h + x.sch_h + x.io_h + x.sec_h + x.cmp_h + x.drv_h) / 6u;
    x.agg_r = 100u - x.agg_h;

    u32 mins = x.mem_h;
    x.bottleneck = 0u;
    if (x.sch_h < mins) { mins = x.sch_h; x.bottleneck = 1u; }
    if (x.io_h < mins) { mins = x.io_h; x.bottleneck = 2u; }
    if (x.sec_h < mins) { mins = x.sec_h; x.bottleneck = 3u; }
    if (x.cmp_h < mins) { mins = x.cmp_h; x.bottleneck = 4u; }
    if (x.drv_h < mins) { mins = x.drv_h; x.bottleneck = 5u; }

    x.mode_hint = 0u;
    if (x.agg_r > 70u || x.sec_h < 40u) x.mode_hint = 2u;
    else if (x.agg_r > 40u || x.io_h < 45u || x.sch_h < 45u) x.mode_hint = 1u;

    g_agg_h = x.agg_h;
    g_agg_r = x.agg_r;
    g_bneck = x.bottleneck;
    g_mode = x.mode_hint;
    g_ring[g_head] = x;
    g_head = (g_head + 1u) % FL_RING;
    if (g_count < FL_RING) g_count++;
}

void fusionlab_snapshot(fusionlab_snapshot_t* out) {
    if (!out) return;
    fl_sample_t x;
    if (g_count == 0u) {
        x.tick = timer_ticks();
        x.mem_h = x.sch_h = x.io_h = x.sec_h = x.cmp_h = x.drv_h = 100u;
        x.agg_h = g_agg_h; x.agg_r = g_agg_r; x.bottleneck = g_bneck; x.mode_hint = g_mode;
    } else {
        x = g_ring[(g_head + FL_RING - 1u) % FL_RING];
    }
    out->samples = g_count;
    out->window = FL_RING;
    out->mem_health = x.mem_h;
    out->sched_health = x.sch_h;
    out->io_health = x.io_h;
    out->sec_health = x.sec_h;
    out->compat_health = x.cmp_h;
    out->driver_health = x.drv_h;
    out->aggregate_health = x.agg_h;
    out->aggregate_risk = x.agg_r;
    out->bottleneck = x.bottleneck;
    out->mode_hint = x.mode_hint;
    out->last_tick = x.tick;
}

int fusionlab_validate(fusionlab_validation_t* out) {
    fusionlab_validation_t r = {0,0,0,0};
    #define FLCHECK(cond, err) do { r.checks_total++; if (cond) r.checks_passed++; else { r.checks_failed++; if (!r.first_error) r.first_error = (err);} } while (0)
    FLCHECK(g_head < FL_RING, 7001u);
    FLCHECK(g_count <= FL_RING, 7002u);
    FLCHECK(g_agg_h <= 100u, 7003u);
    FLCHECK(g_agg_r <= 100u, 7004u);
    FLCHECK(g_bneck <= 5u, 7005u);
    FLCHECK(g_mode <= 2u, 7006u);
    if (out) *out = r;
    return r.checks_failed ? -1 : 0;
}

int fusionlab_run(u32 rounds, fusionlab_run_result_t* out) {
    if (rounds < 16u) rounds = 16u;
    if (rounds > 512u) rounds = 512u;
    fusionlab_run_result_t r;
    r.rounds = rounds;
    r.risk_before = g_agg_r;
    r.risk_after = g_agg_r;
    r.health_before = g_agg_h;
    r.health_after = g_agg_h;
    r.failures = 0u;

    for (u32 i = 0; i < rounds; i++) {
        if ((i % 2u) == 0u) memlab_tick();
        if ((i % 3u) == 0u) iolab_tick();
        if ((i % 4u) == 0u) seclab_tick();
        if ((i % 6u) == 0u) driverlab_tick();
        fusionlab_tick();
    }
    fusionlab_tick();
    r.risk_after = g_agg_r;
    r.health_after = g_agg_h;
    if (out) *out = r;
    return 0;
}
