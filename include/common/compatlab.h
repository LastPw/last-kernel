#ifndef COMPATLAB_H
#define COMPATLAB_H

#include "types.h"

typedef struct {
    u32 samples;
    u32 window;
    u32 tracked_processes;
    u32 fast_hits;
    u32 slow_hits;
    u32 hit_ratio_percent;
    u32 abi_conflicts;
    u32 cache_entries;
    u32 dll_sync_score;
    u32 dll_desync_events;
    u32 sync_pressure;
    u32 risk_score;
    u32 health_score;
    u32 mode_hint;
    u32 last_tick;
} compatlab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} compatlab_validation_t;

typedef struct {
    u32 mode;
    u32 rounds;
    u32 fast_before;
    u32 fast_after;
    u32 slow_before;
    u32 slow_after;
    u32 conflicts_before;
    u32 conflicts_after;
    u32 risk_before;
    u32 risk_after;
    u32 failures;
} compatlab_run_result_t;

void compatlab_init(void);
void compatlab_reset(void);
void compatlab_tick(void);
void compatlab_snapshot(compatlab_snapshot_t* out);
int compatlab_validate(compatlab_validation_t* out);
int compatlab_run(u32 mode, u32 rounds, compatlab_run_result_t* out);

#endif
