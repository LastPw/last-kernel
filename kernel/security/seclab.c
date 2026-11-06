#include "seclab.h"
#include "kshield.h"
#include "sdc.h"
#include "antivirus.h"
#include "threat_logger.h"
#include "timer.h"

#define SECLAB_RING 256u

typedef struct {
    u32 tick;
    u32 threat_count;
    kshield_status_t ks;
    sdc_status_t sdc;
    antivirus_status_t av;
    u32 pressure;
    u32 containment;
    u32 risk;
    u32 health;
    u32 mode_hint;
} seclab_sample_t;

typedef struct {
    seclab_sample_t ring[SECLAB_RING];
    u32 head;
    u32 count;
    u32 pressure;
    u32 containment;
    u32 risk;
    u32 health;
    u32 mode_hint;
} seclab_state_t;

static seclab_state_t g;

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

void seclab_init(void) {
    g.head = 0u;
    g.count = 0u;
    g.pressure = 0u;
    g.containment = 100u;
    g.risk = 0u;
    g.health = 100u;
    g.mode_hint = 0u;
    for (u32 i = 0u; i < SECLAB_RING; i++) {
        g.ring[i].tick = 0u;
    }
}

void seclab_reset(void) {
    seclab_init();
}

void seclab_tick(void) {
    seclab_sample_t s;
    s.tick = timer_ticks();
    s.threat_count = threat_logger_count();
    kshield_status(&s.ks);
    sdc_status(&s.sdc);
    antivirus_status(&s.av);

    u32 pressure = 0u;
    pressure += s.ks.risk_score / 3u;
    pressure += s.sdc.alerts_total > 100u ? 30u : (s.sdc.alerts_total / 4u);
    pressure += s.av.network_anomalies > 80u ? 25u : (s.av.network_anomalies / 4u);
    pressure = clamp100(pressure);

    u32 containment = 0u;
    containment += (s.ks.lockdown_level * 20u);
    containment += (s.sdc.quarantined_tasks > 0u) ? 20u : 0u;
    containment += (s.av.filesystem_integrity_percent > 70u) ? 20u : (s.av.filesystem_integrity_percent / 5u);
    containment += (s.ks.integrity_score > 70u) ? 20u : (s.ks.integrity_score / 5u);
    containment = clamp100(containment);

    u32 risk = pressure;
    if (s.av.active_threats > 0u) risk += 20u;
    if (s.sdc.network_suspicious > 0u) risk += 10u;
    if (containment < 40u) risk += 20u;
    risk = clamp100(risk);

    u32 health = 100u - risk;
    u32 hint = 0u; /* desktop */
    if (risk > 70u || s.av.active_threats > 0u) hint = 1u; /* server hardening */
    if (s.ks.lockdown_level >= 2u && risk > 80u) hint = 2u; /* lockdown */

    s.pressure = pressure;
    s.containment = containment;
    s.risk = risk;
    s.health = health;
    s.mode_hint = hint;

    g.pressure = pressure;
    g.containment = containment;
    g.risk = risk;
    g.health = health;
    g.mode_hint = hint;

    g.ring[g.head] = s;
    g.head = (g.head + 1u) % SECLAB_RING;
    if (g.count < SECLAB_RING) g.count++;
}

void seclab_snapshot(seclab_snapshot_t* out) {
    if (!out) return;
    seclab_sample_t s;
    if (g.count == 0u) {
        s.tick = timer_ticks();
        s.threat_count = threat_logger_count();
        kshield_status(&s.ks);
        sdc_status(&s.sdc);
        antivirus_status(&s.av);
        s.pressure = g.pressure;
        s.containment = g.containment;
        s.risk = g.risk;
        s.health = g.health;
        s.mode_hint = g.mode_hint;
    } else {
        s = g.ring[(g.head + SECLAB_RING - 1u) % SECLAB_RING];
    }

    u32 rate = 0u;
    if (g.count > 1u) {
        u32 first = (g.head + SECLAB_RING - g.count) % SECLAB_RING;
        u32 dt = s.tick - g.ring[first].tick;
        if (dt > 0u && s.threat_count >= g.ring[first].threat_count) {
            rate = ((s.threat_count - g.ring[first].threat_count) * 100u) / dt;
        }
    }

    out->samples = g.count;
    out->window = SECLAB_RING;
    out->threat_count = s.threat_count;
    out->threat_rate = rate;
    out->kshield_risk = s.ks.risk_score;
    out->kshield_integrity = s.ks.integrity_score;
    out->kshield_lockdown = s.ks.lockdown_level;
    out->sdc_alerts = s.sdc.alerts_total;
    out->sdc_quarantined = s.sdc.quarantined_tasks;
    out->av_active_threats = s.av.active_threats;
    out->av_network_anomalies = s.av.network_anomalies;
    out->av_integrity_percent = s.av.filesystem_integrity_percent;
    out->pressure_score = s.pressure;
    out->containment_score = s.containment;
    out->risk_score = s.risk;
    out->health_score = s.health;
    out->mode_hint = s.mode_hint;
    out->last_tick = s.tick;
}

int seclab_validate(seclab_validation_t* out) {
    seclab_validation_t r;
    r.checks_total = 0u;
    r.checks_passed = 0u;
    r.checks_failed = 0u;
    r.first_error = 0u;

    #define SECLAB_CHECK(cond, err) do { \
        r.checks_total++; \
        if (cond) r.checks_passed++; \
        else { \
            r.checks_failed++; \
            if (r.first_error == 0u) r.first_error = (err); \
        } \
    } while (0)

    SECLAB_CHECK(g.head < SECLAB_RING, 4001u);
    SECLAB_CHECK(g.count <= SECLAB_RING, 4002u);
    SECLAB_CHECK(g.pressure <= 100u, 4003u);
    SECLAB_CHECK(g.containment <= 100u, 4004u);
    SECLAB_CHECK(g.risk <= 100u, 4005u);
    SECLAB_CHECK(g.health <= 100u, 4006u);
    SECLAB_CHECK(g.mode_hint <= 2u, 4007u);

    if (g.count > 1u) {
        u32 first = (g.head + SECLAB_RING - g.count) % SECLAB_RING;
        u32 prev = g.ring[first].tick;
        for (u32 i = 1u; i < g.count; i++) {
            u32 idx = (first + i) % SECLAB_RING;
            SECLAB_CHECK(g.ring[idx].tick >= prev, 4008u);
            prev = g.ring[idx].tick;
        }
    } else {
        SECLAB_CHECK(1, 0u);
    }

    if (out) *out = r;
    return (r.checks_failed == 0u) ? 0 : -1;
}

int seclab_run(u32 mode, u32 rounds, seclab_run_result_t* out) {
    if (rounds < 16u) rounds = 16u;
    if (rounds > 2048u) rounds = 2048u;

    seclab_run_result_t r;
    r.mode = mode;
    r.rounds = rounds;
    r.threats_before = threat_logger_count();
    r.threats_after = r.threats_before;
    r.risk_before = g.risk;
    r.risk_after = g.risk;
    r.health_before = g.health;
    r.health_after = g.health;
    r.failures = 0u;

    for (u32 i = 0u; i < rounds; i++) {
        if (mode == 0u) {
            if ((i % 11u) == 0u) {
                threat_logger_log(THREAT_IO_MALFORMED, 0u, i, 0u);
            }
        } else if (mode == 1u) {
            if ((i % 7u) == 0u) {
                threat_logger_log(THREAT_IRQ_ABUSE, 0u, i, 1u);
            }
            if ((i % 9u) == 0u) {
                threat_logger_log(THREAT_CPU_ABUSE, 0u, i, 2u);
            }
        } else {
            if ((i % 5u) == 0u) {
                threat_logger_log(THREAT_KERNEL_WRITE, 0u, i, 3u);
            }
            if ((i % 13u) == 0u) {
                threat_logger_log(THREAT_STACK_CORRUPT, 0u, i, 4u);
            }
        }
        if ((i % 2u) == 0u) {
            seclab_tick();
        }
    }
    seclab_tick();

    r.threats_after = threat_logger_count();
    r.risk_after = g.risk;
    r.health_after = g.health;
    if (out) *out = r;
    return (r.failures == 0u) ? 0 : -1;
}
