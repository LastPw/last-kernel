#include "metrics.h"
#include "timer.h"
#include "power.h"
#include "frame_allocator.h"
#include "heap.h"
#include "interrupts.h"
#include "scheduler.h"
#include "multicore_scheduler.h"
#include "audit.h"
#include "security.h"
#include "threat_logger.h"
#include "swap.h"
#include "io_scheduler.h"
#include "oom.h"
#include "ultimate_kernel.h"
#include "pcoe2.h"
#include "vm.h"
#include "watchdog.h"

static u32 g_ram_bytes = 0;

void metrics_init(u32 ram_bytes) {
    g_ram_bytes = ram_bytes;
}

void metrics_snapshot(kernel_metrics_t* out) {
    if (!out) {
        return;
    }

    u32 busy[METRICS_MAX_CORES];
    u32 total[METRICS_MAX_CORES];
    mcs_snapshot(busy, total, METRICS_MAX_CORES);

    out->ram_bytes = g_ram_bytes;
    out->ticks = timer_ticks();
    out->timer_hz = power_timer_hz();
    out->irq_count = interrupts_total_count();
    out->core_count = mcs_core_count();

    for (u32 i = 0; i < METRICS_MAX_CORES; i++) {
        if (i < out->core_count && total[i] > 0) {
            out->cpu_usage_percent[i] = (busy[i] * 100) / total[i];
        } else {
            out->cpu_usage_percent[i] = 0;
        }
    }

    out->context_switches = scheduler_context_switches();
    out->task_count = scheduler_task_count();
    out->scheduler_quantum_level = scheduler_quantum_level();
    out->scheduler_quantum_ticks = scheduler_quantum_ticks();
    ukernel_status_t uk;
    ukernel_status(&uk);
    out->ukernel_mode = (u32)uk.mode;
    out->ukernel_perf_score = uk.perf_score;
    out->ukernel_risk_score = uk.risk_score;
    out->ukernel_thermal_score = uk.thermal_score;
    out->ukernel_memory_pressure = uk.memory_pressure;
    out->ukernel_network_pressure = uk.network_pressure;
    out->ukernel_latency_shadow = uk.latency_shadow_level;
    out->ukernel_lockdown = uk.sandbox_lockdown;
    out->ukernel_self_heal_actions = uk.self_heal_actions;
    pcoe2_status_t p2;
    pcoe2_status(&p2);
    out->pcoe_mode = (u32)p2.mode;
    out->pcoe_prs_score = p2.prs_score;
    out->pcoe_dmca_score = p2.dmca_score;
    out->pcoe_ghost_score = p2.ghost_score;
    out->pcoe_ai_sched_score = p2.ai_sched_score;
    out->pcoe_clroe_score = p2.clroe_score;
    out->pcoe_security_sandbox_score = p2.security_sandbox_score;
    out->pcoe_hybrid_mode_score = p2.hybrid_mode_score;
    out->pcoe_ghost_gpu_score = p2.ghost_gpu_score;
    out->pcoe_energy_thermal_score = p2.energy_thermal_score;
    out->pcoe_learning_score = p2.handcrafted_learning_score;
    out->pcoe_quantum_predictive_score = p2.quantum_predictive_score;
    out->pcoe_core_anomaly_score = p2.core_anomaly_score;
    out->pcoe_core_stability_score = p2.core_stability_score;
    out->pcoe_core_predicted_pressure = p2.core_predicted_pressure;
    out->pcoe_hyper_ghost_pressure = p2.hyper_ghost_pressure;
    out->pcoe_hyper_ghost_latency_shadow = p2.hyper_ghost_latency_shadow;
    out->pcoe_hyper_ghost_reserved_kb = p2.hyper_ghost_reserved_kb;
    out->pcoe_hyper_ghost_hot_pool_kb = p2.hyper_ghost_hot_pool_kb;
    out->pcoe_hyper_ghost_warm_pool_kb = p2.hyper_ghost_warm_pool_kb;
    out->pcoe_hyper_ghost_cold_pool_kb = p2.hyper_ghost_cold_pool_kb;
    out->pcoe_hyper_ghost_shadow_pages = p2.hyper_ghost_shadow_pages;
    out->pcoe_hyper_ghost_compression_permille = p2.hyper_ghost_compression_permille;
    out->pcoe_hyper_ghost_dedup_permille = p2.hyper_ghost_dedup_permille;
    out->pcoe_hyper_ghost_reclaim_events = p2.hyper_ghost_reclaim_events;
    out->pcoe_hyper_ghost_rebalance_events = p2.hyper_ghost_rebalance_events;
    out->pcoe_quantum_score = p2.quantum_score;
    out->pcoe_quantum_pressure = p2.quantum_pressure;
    out->pcoe_quantum_anomaly = p2.quantum_anomaly;
    out->pcoe_quantum_stability = p2.quantum_stability;
    out->pcoe_quantum_preempt_hint = p2.quantum_preempt_hint;
    out->pcoe_quantum_throughput_gain = p2.quantum_throughput_gain;
    out->pcoe_quantum_fairness_index = p2.quantum_fairness_index;
    out->pcoe_quantum_predicted_threads = p2.quantum_predicted_threads;
    out->pcoe_quantum_dependency_depth = p2.quantum_dependency_depth;
    out->pcoe_quantum_clone_level = p2.quantum_clone_level;
    out->pcoe_quantum_micro_thread_level = p2.quantum_micro_thread_level;
    out->pcoe_latency_shadow = p2.latency_shadow_level;
    out->pcoe_quantum_level = p2.scheduler_quantum_level;
    out->pcoe_dependency_depth = p2.predicted_dependency_depth;
    out->pcoe_clone_level = p2.adaptive_clone_level;
    out->pcoe_micro_thread_level = p2.micro_thread_level;
    out->pcoe_preload_level = p2.preload_level;
    out->pcoe_security_pressure = p2.security_pressure;
    watchdog_status_t wd;
    watchdog_status(&wd);
    out->watchdog_stalls = wd.stalls_detected;
    out->watchdog_heap_faults = wd.heap_faults;
    out->watchdog_vm_oom_spikes = wd.vm_oom_spikes;
    out->watchdog_mitigation_actions = wd.mitigation_actions;
    out->watchdog_healthy = wd.healthy;
    out->paniclog_count = 0u;
    out->compat_enabled = 0u;
    out->compat_tracked_processes = 0u;
    out->compat_fast_hits = 0u;
    out->compat_slow_hits = 0u;
    out->compat_abi_conflicts = 0u;
    out->compat_astg_promotions = 0u;
    out->compat_quarantined_profiles = 0u;
    out->compat_cache_entries = 0u;
    vm_stats_t vs;
    vm_stats(&vs);
    out->vm_active_regions = vs.active_regions;
    out->vm_total_faults = vs.total_faults;
    out->vm_handled_faults = vs.handled_faults;
    out->vm_protection_faults = vs.protection_faults;
    out->vm_guard_faults = vs.guard_faults;
    out->vm_oom_faults = vs.oom_faults;
    out->vm_committed_pages = vs.committed_pages;
    out->frames_total = frame_total();
    out->frames_used = frame_used();
    out->heap_used = heap_used_bytes();
    out->heap_peak = heap_peak_bytes();
    out->heap_corruptions = heap_corruption_count();
    out->heap_fragmentation = heap_fragmentation_score();
    out->audit_violations = audit_violation_count();
    out->security_score = 100u;
    out->threat_count = threat_logger_count();
    out->blocked_tasks = 0u;
    out->swap_used_kb = swap_used_kb();
    io_stats_t io;
    io_scheduler_stats(&io);
    out->io_ops_total = io.ops_total;
    out->io_queue_depth = io.queue_depth;
    out->net_packets = io.net_packets;
    out->oom_kills = oom_kill_count();
    out->power_mode = (u32)power_get_mode();

    const security_profile_t* sp = security_profile();
    out->nx_active = sp->nx_active;
    out->smep_active = sp->smep_active;
    out->smap_active = sp->smap_active;
}
