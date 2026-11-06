#ifndef SCHEDLAB_H
#define SCHEDLAB_H

#include "types.h"

typedef struct {
    u32 samples;
    u32 window;
    u32 active_tasks;
    u32 runqueue_avg;
    u32 runqueue_peak;
    u32 switches_total;
    u32 switch_rate;
    u32 slice_goal_avg;
    u32 quantum_level_avg;
    u32 fairness_score;
    u32 starvation_score;
    u32 pressure_score;
    u32 risk_score;
    u32 health_score;
    u32 last_tick;
} schedlab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} schedlab_validation_t;

typedef struct {
    u32 rounds;
    u32 mode;
    u32 switches_before;
    u32 switches_after;
    u32 fairness_before;
    u32 fairness_after;
    u32 risk_before;
    u32 risk_after;
    u32 failures;
} schedlab_run_result_t;

void schedlab_init(void);
void schedlab_reset(void);
void schedlab_tick(u32 current_task_id, u32 core_id, u32 is_busy, u32 runqueue_depth, u32 slice_goal, u32 quantum_level);
void schedlab_on_switch(u32 from_task, u32 to_task, u32 core_id);
void schedlab_snapshot(schedlab_snapshot_t* out);
int schedlab_validate(schedlab_validation_t* out);
int schedlab_run(u32 mode, u32 rounds, schedlab_run_result_t* out);

#endif
