#include "driverlab.h"
#include "driver_bus.h"
#include "hal.h"
#include "timer.h"

#define DL_RING 256u

typedef struct {
    u32 tick;
    driver_bus_stats_t db;
    hal_status_t hs;
    u32 critical_active;
    u32 sandboxed_count;
    u32 pressure;
    u32 risk;
    u32 health;
    u32 mode_hint;
} dl_sample_t;

static dl_sample_t g_ring[DL_RING];
static u32 g_head = 0u, g_count = 0u;
static u32 g_risk = 0u, g_health = 100u, g_mode_hint = 0u;

static u32 clamp100(u32 v) { return v > 100u ? 100u : v; }

void driverlab_init(void) {
    g_head = 0u; g_count = 0u; g_risk = 0u; g_health = 100u; g_mode_hint = 0u;
    for (u32 i = 0; i < DL_RING; i++) g_ring[i].tick = 0u;
}

void driverlab_reset(void) { driverlab_init(); }

void driverlab_tick(void) {
    dl_sample_t s;
    s.tick = timer_ticks();
    driver_bus_stats(&s.db);
    hal_status(&s.hs);
    s.critical_active = 0u;
    s.sandboxed_count = 0u;
    for (u32 i = 0; i < driver_count(); i++) {
        driver_info_t d;
        if (driver_info_by_index(i, &d) != 0) continue;
        if (d.active && d.critical) s.critical_active++;
        if (d.sandboxed) s.sandboxed_count++;
    }
    u32 pressure = 0u;
    if (s.db.avg_health < 60u) pressure += (60u - s.db.avg_health);
    pressure += (s.db.total_failures > 50u) ? 30u : (s.db.total_failures / 2u);
    pressure += (s.db.total_restarts > 40u) ? 20u : (s.db.total_restarts / 2u);
    pressure = clamp100(pressure);
    u32 risk = pressure / 2u;
    if (s.critical_active == 0u) risk += 30u;
    if (s.sandboxed_count < s.db.installed / 2u) risk += 20u;
    risk = clamp100(risk);
    s.pressure = pressure;
    s.risk = risk;
    s.health = 100u - risk;
    s.mode_hint = (risk > 70u) ? 2u : ((risk > 40u) ? 1u : 0u);
    g_risk = s.risk; g_health = s.health; g_mode_hint = s.mode_hint;
    g_ring[g_head] = s;
    g_head = (g_head + 1u) % DL_RING;
    if (g_count < DL_RING) g_count++;
}

void driverlab_snapshot(driverlab_snapshot_t* out) {
    if (!out) return;
    dl_sample_t s;
    if (g_count == 0u) {
        s.tick = timer_ticks();
        driver_bus_stats(&s.db);
        hal_status(&s.hs);
        s.critical_active = 0u;
        s.sandboxed_count = 0u;
        s.pressure = 0u;
        s.risk = g_risk;
        s.health = g_health;
        s.mode_hint = g_mode_hint;
    } else {
        s = g_ring[(g_head + DL_RING - 1u) % DL_RING];
    }
    out->samples = g_count;
    out->window = DL_RING;
    out->installed = s.db.installed;
    out->active = s.db.active;
    out->total_failures = s.db.total_failures;
    out->total_restarts = s.db.total_restarts;
    out->avg_health = s.db.avg_health;
    out->critical_active = s.critical_active;
    out->sandboxed_count = s.sandboxed_count;
    out->hal_driver_avg_health = s.hs.driver_avg_health;
    out->hal_recoveries = s.hs.driver_recovery_actions;
    out->pressure_score = s.pressure;
    out->risk_score = s.risk;
    out->health_score = s.health;
    out->mode_hint = s.mode_hint;
    out->last_tick = s.tick;
}

int driverlab_validate(driverlab_validation_t* out) {
    driverlab_validation_t r = {0,0,0,0};
    #define DLCHECK(cond, err) do { r.checks_total++; if (cond) r.checks_passed++; else { r.checks_failed++; if (!r.first_error) r.first_error = (err);} } while (0)
    DLCHECK(g_head < DL_RING, 6001u);
    DLCHECK(g_count <= DL_RING, 6002u);
    DLCHECK(g_risk <= 100u, 6003u);
    DLCHECK(g_health <= 100u, 6004u);
    DLCHECK(g_mode_hint <= 2u, 6005u);
    if (out) *out = r;
    return r.checks_failed ? -1 : 0;
}

int driverlab_run(u32 mode, u32 rounds, driverlab_run_result_t* out) {
    if (rounds < 16u) rounds = 16u;
    if (rounds > 1024u) rounds = 1024u;
    driver_bus_stats_t b, a;
    driver_bus_stats(&b);
    driverlab_run_result_t r;
    r.mode = mode;
    r.rounds = rounds;
    r.failures_before = b.total_failures;
    r.failures_after = b.total_failures;
    r.restarts_before = b.total_restarts;
    r.restarts_after = b.total_restarts;
    r.risk_before = g_risk;
    r.risk_after = g_risk;
    r.failures = 0u;
    for (u32 i = 0; i < rounds; i++) {
        const char* name = ((i % 4u) == 0u) ? "nic" : (((i % 4u) == 1u) ? "timer" : (((i % 4u) == 2u) ? "keyboard" : "screen"));
        if (mode == 0u) {
            if (driver_ioctl(name, 1u, i) != 0) r.failures++;
        } else if (mode == 1u) {
            u8 buf[8];
            if (driver_read(name, buf, sizeof(buf)) != 0) r.failures++;
            if (driver_write(name, buf, sizeof(buf)) != 0) r.failures++;
        } else {
            if ((i % 11u) == 0u) (void)driver_recover(name);
        }
        if ((i % 3u) == 0u) driverlab_tick();
    }
    driverlab_tick();
    driver_bus_stats(&a);
    r.failures_after = a.total_failures;
    r.restarts_after = a.total_restarts;
    r.risk_after = g_risk;
    if (out) *out = r;
    return (r.failures == 0u) ? 0 : -1;
}
