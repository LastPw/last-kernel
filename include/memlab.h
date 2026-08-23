#ifndef MEMLAB_H
#define MEMLAB_H

#include "types.h"

typedef struct {
    u32 samples_collected;
    u32 sample_window;
    u32 heap_used_avg;
    u32 heap_frag_avg;
    u32 frame_used_avg;
    u32 frame_frag_avg;
    u32 frame_largest_order;
    u32 slab_total_objects;
    u32 slab_free_objects;
    u32 alloc_failures_total;
    u32 alloc_failures_delta;
    u32 fragmentation_peak;
    u32 pressure_score;
    u32 risk_score;
    u32 health_score;
    u32 last_tick;
} memlab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} memlab_validation_t;

typedef struct {
    u32 scenario;
    u32 rounds;
    u32 operations;
    u32 failures;
    u32 bytes_requested;
    u32 bytes_allocated;
    u32 before_risk;
    u32 after_risk;
} memlab_run_result_t;

void memlab_init(void);
void memlab_tick(void);
void memlab_reset(void);
void memlab_snapshot(memlab_snapshot_t* out);
int memlab_validate(memlab_validation_t* out);
int memlab_run(u32 scenario, u32 rounds, u32 payload, memlab_run_result_t* out);
u32 memlab_histogram_bin(u32 idx);
u32 memlab_histogram_bins(void);

#endif
