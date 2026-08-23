#ifndef DRIVERLAB_H
#define DRIVERLAB_H

#include "types.h"

typedef struct {
    u32 samples;
    u32 window;
    u32 installed;
    u32 active;
    u32 total_failures;
    u32 total_restarts;
    u32 avg_health;
    u32 critical_active;
    u32 sandboxed_count;
    u32 hal_driver_avg_health;
    u32 hal_recoveries;
    u32 pressure_score;
    u32 risk_score;
    u32 health_score;
    u32 mode_hint;
    u32 last_tick;
} driverlab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} driverlab_validation_t;

typedef struct {
    u32 mode;
    u32 rounds;
    u32 failures_before;
    u32 failures_after;
    u32 restarts_before;
    u32 restarts_after;
    u32 risk_before;
    u32 risk_after;
    u32 failures;
} driverlab_run_result_t;

void driverlab_init(void);
void driverlab_reset(void);
void driverlab_tick(void);
void driverlab_snapshot(driverlab_snapshot_t* out);
int driverlab_validate(driverlab_validation_t* out);
int driverlab_run(u32 mode, u32 rounds, driverlab_run_result_t* out);

#endif
