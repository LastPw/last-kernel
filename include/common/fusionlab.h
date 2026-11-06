#ifndef FUSIONLAB_H
#define FUSIONLAB_H

#include "types.h"

typedef struct {
    u32 samples;
    u32 window;
    u32 mem_health;
    u32 sched_health;
    u32 io_health;
    u32 sec_health;
    u32 compat_health;
    u32 driver_health;
    u32 aggregate_health;
    u32 aggregate_risk;
    u32 bottleneck;
    u32 mode_hint;
    u32 last_tick;
} fusionlab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} fusionlab_validation_t;

typedef struct {
    u32 rounds;
    u32 risk_before;
    u32 risk_after;
    u32 health_before;
    u32 health_after;
    u32 failures;
} fusionlab_run_result_t;

void fusionlab_init(void);
void fusionlab_reset(void);
void fusionlab_tick(void);
void fusionlab_snapshot(fusionlab_snapshot_t* out);
int fusionlab_validate(fusionlab_validation_t* out);
int fusionlab_run(u32 rounds, fusionlab_run_result_t* out);

#endif
