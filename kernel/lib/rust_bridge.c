#include "rust_bridge.h"
#include "string.h"

typedef struct {
    u32 initialized;
    u32 task_count;
    u32 ready_count;
    u32 last_pick_task;
    u32 core_count;
    u32 rebalance_events;
} rust_sched_snapshot_raw_t;

typedef struct {
    u32 hits;
    u32 misses;
    u32 evictions;
    u32 entries;
    u32 capacity;
} rust_vfs_cache_stats_raw_t;

typedef struct {
    u32 valid;
    u32 node_id;
    u8 flags;
    u8 reserved[3];
    u32 size;
    u32 last_tick_hint;
} rust_vfs_cache_entry_raw_t;

/* Weak symbols allow linking even when Rust static library is not present yet. */
extern void rust_sched_init(u32 cores) __attribute__((weak));
extern void rust_sched_enqueue(u32 task_id, u8 priority, u8 task_class, u32 vruntime_hint, u32 recent_runtime) __attribute__((weak));
extern void rust_sched_task_signals(u32 task_id, u32 home_core, u32 cache_heat, u32 burst_score, u32 pressure, u32 wait_ticks, u32 latency_target, u32 thermal_class, u32 flow_id, u32 wake_predict) __attribute__((weak));
extern void rust_sched_tick(u32 core_id, u32 elapsed_ticks) __attribute__((weak));
extern i32 rust_sched_pick_core(u32 task_id, u32 fallback_core, u32* out_core) __attribute__((weak));
extern i32 rust_sched_pick_next(u32 core_id, u32* out_task_id) __attribute__((weak));
extern i32 rust_sched_advise(u32 task_id, u32 core_id, u32 current_ns, u32 runqueue_depth, u32 pressure_hint, u32 runtime_ticks, i32* out_delta) __attribute__((weak));
extern i32 rust_sched_snapshot(rust_sched_snapshot_raw_t* out) __attribute__((weak));
extern void rust_policy_init(void) __attribute__((weak));
extern i32 rust_policy_ipc_decide(u32 src_task, u32 dst_task, u32 same_ns, u32 queue_depth, u32 lockdown, u32 rate_window_count, u32* out_action, u32* out_score) __attribute__((weak));
extern i32 rust_policy_compat_guard(u32 task_id, u32 sync_score, u32 dll_sync, u32 drift, u32 bridge_conf, u32 sece_trust, u32 lockdown, u32 burst_hit, u32 failures, u32 risky_nr, u32* out_action, u32* out_level, u32* out_penalty) __attribute__((weak));

extern void rust_vfs_cache_init(u32 capacity) __attribute__((weak));
extern i32 rust_vfs_cache_put(const u8* path, u32 path_len, u32 node_id, u8 flags, u32 size) __attribute__((weak));
extern i32 rust_vfs_cache_get(const u8* path, u32 path_len, rust_vfs_cache_entry_raw_t* out) __attribute__((weak));
extern i32 rust_vfs_cache_invalidate(const u8* path, u32 path_len) __attribute__((weak));
extern i32 rust_vfs_cache_stats(rust_vfs_cache_stats_raw_t* out) __attribute__((weak));

extern i32 rust_path_canonicalize(const u8* in_ptr, u32 in_len, u8* out_ptr, u32 out_max) __attribute__((weak));
extern i32 rust_sec_guard_register(u32 task_id, u32 caps) __attribute__((weak));
extern i32 rust_sec_guard_check_cap(u32 task_id, u32 cap) __attribute__((weak));
extern void rust_sec_guard_record_violation(u32 task_id, u16 weight) __attribute__((weak));

extern void rust_mem_init(u64 total_ram_bytes) __attribute__((weak));
extern i32  rust_mem_validate_user_range(u64 addr, u64 len, u32 is_write) __attribute__((weak));
extern void rust_mem_on_alloc(u64 size) __attribute__((weak));
extern void rust_mem_on_free(u64 size) __attribute__((weak));
extern u32  rust_mem_pressure_score(void) __attribute__((weak));
extern i32  rust_mem_snapshot(rust_mem_stats_t* out) __attribute__((weak));

static u8 g_rust_available = 0u;

static u32 strn_len_local(const char* s, u32 max_len) {
    u32 i = 0u;
    if (!s) return 0u;
    while (i < max_len && s[i]) i++;
    return i;
}

void rust_bridge_init(u32 cores) {
    g_rust_available = 0u;
    if (!rust_sched_init || !rust_vfs_cache_init) {
        return;
    }
    rust_sched_init(cores ? cores : 1u);
    rust_vfs_cache_init(256u);
    if (rust_policy_init) {
        rust_policy_init();
    }
    g_rust_available = 1u;
}

u8 rust_bridge_available(void) {
    return g_rust_available;
}

void rust_bridge_sched_on_task(u32 task_id, u8 priority, u8 task_class, u32 vruntime_hint, u32 recent_runtime) {
    if (!g_rust_available || !rust_sched_enqueue) return;
    rust_sched_enqueue(task_id, priority, task_class, vruntime_hint, recent_runtime);
}

void rust_bridge_sched_task_signals(u32 task_id,
                                    u32 home_core,
                                    u32 cache_heat,
                                    u32 burst_score,
                                    u32 pressure,
                                    u32 wait_ticks,
                                    u32 latency_target,
                                    u32 thermal_class,
                                    u32 flow_id,
                                    u32 wake_predict) {
    if (!g_rust_available || !rust_sched_task_signals) return;
    rust_sched_task_signals(
        task_id,
        home_core,
        cache_heat,
        burst_score,
        pressure,
        wait_ticks,
        latency_target,
        thermal_class,
        flow_id,
        wake_predict
    );
}

void rust_bridge_sched_tick(u32 core_id, u32 elapsed_ticks) {
    if (!g_rust_available || !rust_sched_tick) return;
    rust_sched_tick(core_id, elapsed_ticks ? elapsed_ticks : 1u);
}

int rust_bridge_sched_pick_core(u32 task_id, u32 fallback_core, u32* out_core) {
    if (!out_core) return -1;
    *out_core = fallback_core;
    if (!g_rust_available || !rust_sched_pick_core) return -1;
    return (int)rust_sched_pick_core(task_id, fallback_core, out_core);
}

int rust_bridge_sched_pick(u32 core_id, u32* out_task_id) {
    if (!out_task_id) return -1;
    *out_task_id = 0u;
    if (!g_rust_available || !rust_sched_pick_next) return -1;
    return (int)rust_sched_pick_next(core_id, out_task_id);
}

int rust_bridge_sched_advise(u32 task_id, u32 core_id, u32 current_ns, u32 runqueue_depth, u32 pressure_hint, u32 runtime_ticks, i32* out_delta) {
    if (!out_delta) return -1;
    *out_delta = 0;
    if (!g_rust_available || !rust_sched_advise) return -1;
    return (int)rust_sched_advise(task_id, core_id, current_ns, runqueue_depth, pressure_hint, runtime_ticks, out_delta);
}

int rust_bridge_policy_ipc(u32 src_task,
                           u32 dst_task,
                           u32 same_ns,
                           u32 queue_depth,
                           u32 lockdown,
                           u32 rate_window_count,
                           u32* out_action,
                           u32* out_score) {
    if (!out_action || !out_score) return -1;
    *out_action = 0u;
    *out_score = 0u;
    if (!g_rust_available || !rust_policy_ipc_decide) return -1;
    return (int)rust_policy_ipc_decide(
        src_task, dst_task, same_ns, queue_depth, lockdown, rate_window_count, out_action, out_score
    );
}

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
                                    u32* out_penalty) {
    if (!out_action || !out_level || !out_penalty) return -1;
    *out_action = 0u;
    *out_level = 0u;
    *out_penalty = 0u;
    if (!g_rust_available || !rust_policy_compat_guard) return -1;
    return (int)rust_policy_compat_guard(
        task_id,
        sync_score,
        dll_sync,
        drift,
        bridge_conf,
        sece_trust,
        lockdown,
        burst_hit,
        failures,
        risky_nr,
        out_action,
        out_level,
        out_penalty
    );
}

void rust_bridge_sched_snapshot(rust_sched_snapshot_t* out) {
    if (!out) return;
    out->initialized = 0u;
    out->task_count = 0u;
    out->ready_count = 0u;
    out->last_pick_task = 0u;
    out->core_count = 0u;
    out->rebalance_events = 0u;
    if (!g_rust_available || !rust_sched_snapshot) return;

    rust_sched_snapshot_raw_t raw;
    if (rust_sched_snapshot(&raw) != 0) return;
    out->initialized = raw.initialized;
    out->task_count = raw.task_count;
    out->ready_count = raw.ready_count;
    out->last_pick_task = raw.last_pick_task;
    out->core_count = raw.core_count;
    out->rebalance_events = raw.rebalance_events;
}

int rust_bridge_vfs_cache_put(const char* path, u32 node_id, u8 flags, u32 size) {
    u32 n;
    if (!g_rust_available || !rust_vfs_cache_put || !path) return -1;
    n = strn_len_local(path, 255u);
    if (n == 0u) return -1;
    return (int)rust_vfs_cache_put((const u8*)path, n, node_id, flags, size);
}

int rust_bridge_vfs_cache_get(const char* path, rust_vfs_cache_entry_t* out) {
    u32 n;
    rust_vfs_cache_entry_raw_t raw;
    if (!out) return -1;
    out->valid = 0u;
    out->node_id = 0u;
    out->flags = 0u;
    out->size = 0u;
    out->last_tick_hint = 0u;
    if (!g_rust_available || !rust_vfs_cache_get || !path) return -1;
    n = strn_len_local(path, 255u);
    if (n == 0u) return -1;
    if (rust_vfs_cache_get((const u8*)path, n, &raw) != 0) return -1;
    out->valid = raw.valid;
    out->node_id = raw.node_id;
    out->flags = raw.flags;
    out->size = raw.size;
    out->last_tick_hint = raw.last_tick_hint;
    return 0;
}

void rust_bridge_vfs_cache_invalidate(const char* path) {
    u32 n;
    if (!g_rust_available || !rust_vfs_cache_invalidate || !path) return;
    n = strn_len_local(path, 255u);
    if (n == 0u) return;
    (void)rust_vfs_cache_invalidate((const u8*)path, n);
}

void rust_bridge_vfs_cache_stats(rust_vfs_cache_stats_t* out) {
    rust_vfs_cache_stats_raw_t raw;
    if (!out) return;
    out->hits = 0u;
    out->misses = 0u;
    out->evictions = 0u;
    out->entries = 0u;
    out->capacity = 0u;
    if (!g_rust_available || !rust_vfs_cache_stats) return;
    if (rust_vfs_cache_stats(&raw) != 0) return;
    out->hits = raw.hits;
    out->misses = raw.misses;
    out->evictions = raw.evictions;
    out->entries = raw.entries;
    out->capacity = raw.capacity;
}

void rust_bridge_status(rust_bridge_status_t* out) {
    if (!out) return;
    out->available = g_rust_available;
    rust_bridge_sched_snapshot(&out->sched);
    rust_bridge_vfs_cache_stats(&out->vfs_cache);
}

int rust_bridge_path_canonicalize(const char* in_path, u32 in_len, char* out_buf, u32 out_max) {
    if (!g_rust_available || !rust_path_canonicalize || !in_path || !out_buf) return -1;
    return rust_path_canonicalize((const u8*)in_path, (u32)in_len, (u8*)out_buf, (u32)out_max);
}

int rust_bridge_sec_register(u32 task_id, u32 caps) {
    if (!g_rust_available || !rust_sec_guard_register) return -1;
    return rust_sec_guard_register(task_id, caps);
}

int rust_bridge_sec_check_cap(u32 task_id, u32 cap) {
    if (!g_rust_available || !rust_sec_guard_check_cap) return 0;
    return rust_sec_guard_check_cap(task_id, cap);
}

void rust_bridge_sec_record_violation(u32 task_id, u16 weight) {
    if (!g_rust_available || !rust_sec_guard_record_violation) return;
    rust_sec_guard_record_violation(task_id, weight);
}

void rust_bridge_mem_init(u64 total_ram_bytes) {
    if (rust_mem_init) {
        rust_mem_init(total_ram_bytes);
    }
}

int rust_bridge_mem_validate_user_range(u64 addr, u64 len, u32 is_write) {
    if (!g_rust_available || !rust_mem_validate_user_range) return 1;
    return rust_mem_validate_user_range(addr, len, is_write);
}

void rust_bridge_mem_on_alloc(u64 size) {
    if (g_rust_available && rust_mem_on_alloc) {
        rust_mem_on_alloc(size);
    }
}

void rust_bridge_mem_on_free(u64 size) {
    if (g_rust_available && rust_mem_on_free) {
        rust_mem_on_free(size);
    }
}

u32 rust_bridge_mem_pressure_score(void) {
    if (!g_rust_available || !rust_mem_pressure_score) return 0;
    return rust_mem_pressure_score();
}

int rust_bridge_mem_snapshot(rust_mem_stats_t* out) {
    if (!g_rust_available || !rust_mem_snapshot || !out) return -1;
    return rust_mem_snapshot(out);
}
