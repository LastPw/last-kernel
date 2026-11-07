/*
 * Author: Taha Vaziry
 * File: include/common/resource_monitor.h
 * Description: Lightweight kernel resource telemetry snapshot structures.
 */

#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include "types.h"
#include "frame_allocator.h"

typedef struct {
    u32 cpu_busy_percent;
    u32 mem_used_bytes;
    u32 mem_total_bytes;
    u32 io_ops_total;
    u32 io_queue_depth;
    u32 net_packets;
    u32 swap_used_kb;
    u32 oom_kills;
    u32 thermal_celsius;
    u32 overcommit_permille;
    u32 low_mem_warning;
    u32 predicted_next_alloc;
    u32 recommended_threads;
    u32 recommended_mode;
    u32 profile;
    u32 reclaim_hint_bytes;
} resource_snapshot_t;


static inline void resource_monitor_snapshot(resource_snapshot_t* out) {
    if (!out) return;
    out->cpu_busy_percent = 5u;
    out->mem_used_bytes = frame_used() * 4096u;
    out->mem_total_bytes = frame_total() * 4096u;
    out->io_ops_total = 0u;
    out->io_queue_depth = 0u;
    out->net_packets = 0u;
    out->swap_used_kb = 0u;
    out->oom_kills = 0u;
    out->thermal_celsius = 42u;
    out->overcommit_permille = 0u;
    out->low_mem_warning = 0u;
    out->predicted_next_alloc = 4096u;
    out->recommended_threads = 4u;
    out->recommended_mode = 0u;
    out->profile = 0u;
    out->reclaim_hint_bytes = 0u;
}

#endif /* RESOURCE_MONITOR_H */
