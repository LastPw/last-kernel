#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include "types.h"

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

void resource_monitor_init(void);
void resource_monitor_tick(void);
void resource_monitor_snapshot(resource_snapshot_t* out);

#endif
