#ifndef SECLAB_H
#define SECLAB_H

#include "types.h"

typedef struct {
    u32 samples;
    u32 window;
    u32 threat_count;
    u32 threat_rate;
    u32 kshield_risk;
    u32 kshield_integrity;
    u32 kshield_lockdown;
    u32 sdc_alerts;
    u32 sdc_quarantined;
    u32 av_active_threats;
    u32 av_network_anomalies;
    u32 av_integrity_percent;
    u32 pressure_score;
    u32 containment_score;
    u32 risk_score;
    u32 health_score;
    u32 mode_hint;
    u32 last_tick;
} seclab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} seclab_validation_t;

typedef struct {
    u32 mode;
    u32 rounds;
    u32 threats_before;
    u32 threats_after;
    u32 risk_before;
    u32 risk_after;
    u32 health_before;
    u32 health_after;
    u32 failures;
} seclab_run_result_t;

void seclab_init(void);
void seclab_reset(void);
void seclab_tick(void);
void seclab_snapshot(seclab_snapshot_t* out);
int seclab_validate(seclab_validation_t* out);
int seclab_run(u32 mode, u32 rounds, seclab_run_result_t* out);

#endif
