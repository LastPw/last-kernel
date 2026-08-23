#ifndef SCHED_RUNTIME_H
#define SCHED_RUNTIME_H

#include "types.h"

typedef enum {
    SCHED_TEMP_COLD = 0,
    SCHED_TEMP_WARM = 1,
    SCHED_TEMP_HOT = 2
} sched_temp_queue_t;

typedef enum {
    SCHED_FAST_NONE = 0,
    SCHED_FAST_RENDER = 1,
    SCHED_FAST_CURSOR = 2,
    SCHED_FAST_AUDIO = 3,
    SCHED_FAST_INPUT = 4
} sched_fast_queue_t;

typedef struct {
    u32 task_id;
    u32 task_class;
    u32 realtime;
    u32 thermal_class;
    u32 latency_budget;
    u32 wake_frequency;
    u32 cache_heat;
    u32 interaction_score;
    u32 pressure;
} sched_task_model_t;

typedef struct {
    sched_temp_queue_t temp_queue;
    sched_fast_queue_t fast_queue;
    u32 preempt_boost;
    u32 wake_cost;
    u32 lock_cost;
    u32 migration_limit;
} sched_queue_decision_t;

typedef struct {
    u32 hot_depth;
    u32 warm_depth;
    u32 cold_depth;
    u32 render_depth;
    u32 cursor_depth;
    u32 audio_depth;
    u32 input_depth;
    u32 latency_spikes;
    u32 core_pressure;
    u32 idle_ratio;
    u32 fast_path_hits;
    u32 migration_limited;
} sched_runtime_snapshot_t;

void sched_runtime_init(void);
void sched_runtime_begin_sample(u32 core_count, u32 idle_cores);
sched_queue_decision_t sched_runtime_classify(const sched_task_model_t* task);
void sched_runtime_note_task(const sched_queue_decision_t* decision, u32 wait_ticks, u32 pressure);
void sched_runtime_finish_sample(u32 core_pressure);
void sched_runtime_snapshot(sched_runtime_snapshot_t* out);

#endif
