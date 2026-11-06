#include "resource_monitor.h"
#include "metrics.h"
#include "io_scheduler.h"
#include "swap.h"
#include "oom.h"
#include "resource_engine.h"
#include "advanced_allocator.h"
#include "heap.h"
#include "memory_runtime.h"
#include "memlab.h"
#include "iolab.h"
#include "seclab.h"
#include "compatlab.h"
#include "driverlab.h"
#include "fusionlab.h"

static resource_snapshot_t last;

void resource_monitor_init(void) {
    last.cpu_busy_percent = 0;
    last.mem_used_bytes = 0;
    last.mem_total_bytes = 0;
    last.io_ops_total = 0;
    last.io_queue_depth = 0;
    last.net_packets = 0;
    last.swap_used_kb = 0;
    last.oom_kills = 0;
    last.thermal_celsius = 0;
    last.overcommit_permille = 0;
    last.low_mem_warning = 0;
    last.predicted_next_alloc = 0;
    last.recommended_threads = 1;
    last.recommended_mode = POWER_MODE_AUTO;
    last.profile = RE_PROFILE_AUTO;
    last.reclaim_hint_bytes = 0;
}

void resource_monitor_tick(void) {
    kernel_metrics_t m;
    io_stats_t io;
    metrics_snapshot(&m);
    io_scheduler_stats(&io);

    u32 cpu = 0;
    if (m.core_count == 0) {
        m.core_count = 1;
    }
    for (u32 i = 0; i < m.core_count; i++) {
        cpu += m.cpu_usage_percent[i];
    }
    cpu /= m.core_count;
    resource_engine_tick(cpu, m.heap_used, m.ram_bytes, m.core_count);
    resource_engine_snapshot_t rs;
    resource_engine_snapshot(&rs);
    (void)memory_runtime_govern_pressure(m.ram_bytes, m.heap_used, heap_fragmentation_score());

    last.cpu_busy_percent = cpu;
    last.mem_used_bytes = m.heap_used;
    last.mem_total_bytes = m.ram_bytes;
    last.io_ops_total = io.ops_total;
    last.io_queue_depth = io.queue_depth;
    last.net_packets = io.net_packets;
    last.swap_used_kb = swap_used_kb();
    last.oom_kills = oom_kill_count();
    last.thermal_celsius = rs.thermal_celsius;
    last.overcommit_permille = rs.overcommit_permille;
    last.low_mem_warning = rs.low_mem_warning;
    last.predicted_next_alloc = rs.predicted_next_alloc;
    last.recommended_threads = rs.recommended_threads;
    last.recommended_mode = (u32)rs.recommended_mode;
    last.profile = (u32)rs.profile;
    last.reclaim_hint_bytes = adv_reclaim_step((m.ram_bytes * 80u) / 100u);
    memlab_tick();
    iolab_tick();
    seclab_tick();
    compatlab_tick();
    driverlab_tick();
    fusionlab_tick();
}

void resource_monitor_snapshot(resource_snapshot_t* out) {
    if (!out) {
        return;
    }
    *out = last;
}
