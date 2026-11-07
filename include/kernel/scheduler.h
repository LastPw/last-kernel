#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"
#include "idt.h"

typedef struct {
    u32 task_count;
    u32 current_task;
    u32 context_switches;
    u32 core_count;
    u32 quantum_level;
    u32 quantum_ticks;
    u32 avg_slice_goal;
    u32 load_percent;
} scheduler_stats_t;

typedef struct {
    u32 thermal_class_score;
    u32 pulse_path_score;
    u32 migration_predictions;
    u32 latency_core_score;
    u32 cache_heat_score;
    u32 adaptive_quantum;
    u32 burst_prediction_score;
    u32 shadow_idle_cores;
    u32 speculative_wakes;
    u32 flow_placement_score;
    u32 microburst_balance_score;
    u32 realtime_islands;
    u32 energy_latency_score;
    u32 task_pressure;
    u32 desktop_fastlane_score;
    u32 numa_affinity_score;
    u32 hot_thread_isolations;
    u32 queue_temperature;
    u32 cross_core_wake_predictions;
    u32 hot_queue_depth;
    u32 warm_queue_depth;
    u32 cold_queue_depth;
    u32 render_queue_depth;
    u32 cursor_queue_depth;
    u32 audio_queue_depth;
    u32 input_queue_depth;
    u32 latency_spikes;
    u32 idle_ratio;
    u32 fast_path_hits;
    u32 migration_limited;
} scheduler_telemetry_t;

typedef struct {
    u32 task_id;
    u32 home_core;
    u32 cache_heat;
    u32 burst_score;
    u32 pressure;
    u32 latency_budget;
    u32 wake_frequency;
    u32 interaction_score;
    u32 queue_class;
    u32 fast_queue;
    u32 policy;
    u32 latency_score;
    u32 wait_ticks;
    u32 migrations;
} scheduler_task_telemetry_t;

typedef enum {
    SCHED_CLASS_CPU = 0,
    SCHED_CLASS_IO = 1,
    SCHED_CLASS_NET = 2
} sched_class_t;

#define SCHED_TASK_DEAD    0u
#define SCHED_TASK_READY   1u
#define SCHED_TASK_IDLE    2u
#define SCHED_TASK_BLOCKED 3u

typedef enum {
    SCHED_POLICY_CFS = 0,
    SCHED_POLICY_RT = 1,
    SCHED_POLICY_FASTLANE = 2,
    SCHED_POLICY_IDLE = 3
} sched_policy_t;

typedef struct {
    u32 cfs_tasks;
    u32 rt_tasks;
    u32 fastlane_tasks;
    u32 idle_tasks;
} scheduler_policy_stats_t;

void scheduler_init(void);
void scheduler_tick(registers_t* regs);
void scheduler_schedule_from_irq(registers_t* regs);
void scheduler_capture_kernel_context(registers_t* regs);
void scheduler_begin_kernel_resume(void);
void scheduler_request_user_switch(void);
void scheduler_schedule(void);
u32 scheduler_current_task(void);
void scheduler_set_core_count(u32 core_count);
u32 scheduler_context_switches(void);
u32 scheduler_task_count(void);
int scheduler_set_priority(u32 task_id, u8 prio);
int scheduler_set_realtime(u32 task_id, int enabled);
int scheduler_set_cgroup(u32 task_id, u32 cgroup_id);
int scheduler_set_namespace(u32 task_id, u32 ns_id);
int scheduler_set_task_class(u32 task_id, sched_class_t task_class);
sched_class_t scheduler_task_class(u32 task_id);
u32 scheduler_task_priority(u32 task_id);
u64 scheduler_task_vruntime(u32 task_id);
int scheduler_kill_task(u32 task_id);
int scheduler_block_task(u32 task_id);
int scheduler_wake_task(u32 task_id);
u32 scheduler_task_state(u32 task_id);
u32 scheduler_pick_oom_victim(const u8* protected_map, u32 map_len);
void scheduler_set_quantum_level(u32 level);
u32 scheduler_quantum_level(void);
u32 scheduler_quantum_ticks(void);
void scheduler_stats(scheduler_stats_t* out);
void scheduler_telemetry(scheduler_telemetry_t* out);
int scheduler_task_telemetry(u32 task_id, scheduler_task_telemetry_t* out);
void scheduler_policy_stats(scheduler_policy_stats_t* out);
int scheduler_prepare_user_task(u32 task_id, u64 entry, u64 user_stack);
int scheduler_prepare_fork_task(u32 task_id, const registers_t* parent_regs, u32 address_space);
int scheduler_set_address_space(u32 task_id, u32 address_space);
u32 scheduler_task_address_space(u32 task_id);
int scheduler_exit_task(u32 task_id, i32 exit_code);
int scheduler_defer_user_exit(u32 task_id, i32 exit_code);
int scheduler_reap_task(u32 task_id);
int scheduler_current_user_frame(registers_t* out);
int scheduler_task_is_user(u32 task_id);
int scheduler_has_runnable_peer(u32 task_id);
int scheduler_task_sandbox(u32 task_id);
int scheduler_block_wait_from_irq(registers_t* regs, u32 child_task);
int scheduler_create_user_task(const char* name, u32* out_task_id);
int scheduler_create_user_thread(u32 parent_task, u64 entry, u64 user_stack,
                                  const char* name, u32* out_task_id);
int scheduler_restore_user_frame(u32 task_id, const registers_t* frame);

#endif
