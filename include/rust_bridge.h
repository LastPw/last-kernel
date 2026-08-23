#ifndef RUST_BRIDGE_H
#define RUST_BRIDGE_H

#include "types.h"

typedef struct {
    u32 initialized;
    u32 task_count;
    u32 ready_count;
    u32 last_pick_task;
    u32 core_count;
    u32 rebalance_events;
} rust_sched_snapshot_t;

typedef struct {
    u32 hits;
    u32 misses;
    u32 evictions;
    u32 entries;
    u32 capacity;
} rust_vfs_cache_stats_t;

typedef struct {
    u32 valid;
    u32 node_id;
    u8 flags;
    u32 size;
    u32 last_tick_hint;
} rust_vfs_cache_entry_t;

typedef struct {
    u8 available;
    rust_sched_snapshot_t sched;
    rust_vfs_cache_stats_t vfs_cache;
} rust_bridge_status_t;

void rust_bridge_init(u32 cores);
u8 rust_bridge_available(void);

void rust_bridge_sched_on_task(u32 task_id, u8 priority, u8 task_class, u32 vruntime_hint, u32 recent_runtime);
void rust_bridge_sched_task_signals(u32 task_id,
                                    u32 home_core,
                                    u32 cache_heat,
                                    u32 burst_score,
                                    u32 pressure,
                                    u32 wait_ticks,
                                    u32 latency_target,
                                    u32 thermal_class,
                                    u32 flow_id,
                                    u32 wake_predict);
void rust_bridge_sched_tick(u32 core_id, u32 elapsed_ticks);
int rust_bridge_sched_pick_core(u32 task_id, u32 fallback_core, u32* out_core);
int rust_bridge_sched_pick(u32 core_id, u32* out_task_id);
int rust_bridge_sched_advise(u32 task_id, u32 core_id, u32 current_ns, u32 runqueue_depth, u32 pressure_hint, u32 runtime_ticks, i32* out_delta);
void rust_bridge_sched_snapshot(rust_sched_snapshot_t* out);

int rust_bridge_policy_ipc(u32 src_task,
                           u32 dst_task,
                           u32 same_ns,
                           u32 queue_depth,
                           u32 lockdown,
                           u32 rate_window_count,
                           u32* out_action,
                           u32* out_score);
int rust_bridge_policy_compat_guard(u32 task_id,
                                    u32 sync_score,
                                    u32 dll_sync,
                                    u32 drift,
                                    u32 bridge_conf,
                                    u32 sece_trust,
                                    u32 lockdown,
                                    u32 burst_hit,
                                    u32 failures,
                                    u32 risky_nr,
                                    u32* out_action,
                                    u32* out_level,
                                    u32* out_penalty);

int rust_bridge_vfs_cache_put(const char* path, u32 node_id, u8 flags, u32 size);
int rust_bridge_vfs_cache_get(const char* path, rust_vfs_cache_entry_t* out);
void rust_bridge_vfs_cache_invalidate(const char* path);
void rust_bridge_vfs_cache_stats(rust_vfs_cache_stats_t* out);

void rust_bridge_status(rust_bridge_status_t* out);

int rust_bridge_path_canonicalize(const char* in_path, u32 in_len, char* out_buf, u32 out_max);
int rust_bridge_sec_register(u32 task_id, u32 caps);
int rust_bridge_sec_check_cap(u32 task_id, u32 cap);
void rust_bridge_sec_record_violation(u32 task_id, u16 weight);

typedef struct {
    u64 total_ram_bytes;
    u64 allocated_bytes;
    u64 peak_allocated_bytes;
    u32 allocation_count;
    u32 free_count;
    u32 pressure_score;
    u32 fragmentation_ratio;
} rust_mem_stats_t;

void rust_bridge_mem_init(u64 total_ram_bytes);
int  rust_bridge_mem_validate_user_range(u64 addr, u64 len, u32 is_write);
void rust_bridge_mem_on_alloc(u64 size);
void rust_bridge_mem_on_free(u64 size);
u32  rust_bridge_mem_pressure_score(void);
int  rust_bridge_mem_snapshot(rust_mem_stats_t* out);

#endif
