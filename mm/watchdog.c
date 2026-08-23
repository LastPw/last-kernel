#include "watchdog.h"
#include "timer.h"
#include "scheduler.h"
#include "heap.h"
#include "vm.h"
#include "audit.h"
#include "threat_logger.h"
#include "power.h"
#include "process_monitor.h"
#include "paniclog.h"
#include "frame_allocator.h"

static watchdog_status_t st;
static u32 last_ctx_switches = 0;
static u32 stall_ticks = 0;
static u32 last_vm_oom = 0;
static watchdog_policy_t pol;

void watchdog_init(void) {
    st.ticks = 0;
    st.stalls_detected = 0;
    st.heap_faults = 0;
    st.vm_oom_spikes = 0;
    st.last_switch_tick = 0;
    st.healthy = 1;
    st.mitigation_actions = 0;
    st.process_recoveries = 0u;

    last_ctx_switches = scheduler_context_switches();
    stall_ticks = 0;

    vm_stats_t vs;
    vm_stats(&vs);
    last_vm_oom = vs.oom_faults;

    pol.stall_threshold_ticks = 800u;
    pol.vm_oom_spike_threshold = 8u;
    pol.heap_check_period = 64u;
    pol.auto_mitigate = 1u;
}

void watchdog_tick(void) {
    st.ticks = timer_ticks();

    u32 heap_period = pol.heap_check_period ? pol.heap_check_period : 64u;
    if ((st.ticks % heap_period) == 0u) {
        if (heap_validate_integrity() != 0) {
            st.heap_faults++;
            st.healthy = 0;
            audit_log(AUDIT_EVT_HEAP_CORRUPTION, heap_corruption_count(), st.ticks);
            threat_logger_log(THREAT_HEAP_CORRUPT, scheduler_current_task(), heap_corruption_count(), st.ticks);
            if (pol.auto_mitigate) {
                scheduler_set_quantum_level(1u);
                power_set_mode(POWER_MODE_LOW_POWER);
                st.mitigation_actions++;
            }
        }
    }

    if (scheduler_task_count() > 1u) {
        u32 cs = scheduler_context_switches();
        if (cs == last_ctx_switches) {
            stall_ticks++;
            if (stall_ticks > pol.stall_threshold_ticks) {
                st.stalls_detected++;
                st.healthy = 0;
                st.last_switch_tick = st.ticks;
                audit_log(AUDIT_EVT_TASK_VIOLATION, st.stalls_detected, stall_ticks);
                threat_logger_log(THREAT_CPU_ABUSE, scheduler_current_task(), stall_ticks, cs);
                if (pol.auto_mitigate) {
                    scheduler_set_quantum_level(1u);
                    st.mitigation_actions++;
                }
                stall_ticks = 0u;
            }
        } else {
            last_ctx_switches = cs;
            stall_ticks = 0u;
            st.last_switch_tick = st.ticks;
        }
    }

    for (u32 i = 0u; i < process_monitor_task_count(); i++) {
        process_info_t pi;
        if (process_monitor_snapshot_at(i, &pi) != 0 || !pi.blocked || pi.name[0] != '/') continue;
        if (scheduler_kill_task(pi.task_id) == 0) {
            st.process_recoveries++;
            st.mitigation_actions++;
            paniclog_push_for_task(pi.task_id, "watchdog terminated stalled user process",
                                   0xF0u, 0u, 0u, 0u, st.ticks, heap_corruption_count(),
                                   heap_fragmentation_score(), frame_used(), frame_total(),
                                   st.stalls_detected, st.mitigation_actions);
        }
    }

    if ((st.ticks & 0x1Fu) == 0u) {
        vm_stats_t vs;
        vm_stats(&vs);
        if (vs.oom_faults > last_vm_oom + pol.vm_oom_spike_threshold) {
            st.vm_oom_spikes++;
            st.healthy = 0;
            audit_log(AUDIT_EVT_PAGE_FAULT, vs.oom_faults, st.vm_oom_spikes);
            threat_logger_log(THREAT_CPU_ABUSE, scheduler_current_task(), vs.oom_faults, st.vm_oom_spikes);
            if (pol.auto_mitigate) {
                power_set_mode(POWER_MODE_LOW_POWER);
                st.mitigation_actions++;
            }
        }
        last_vm_oom = vs.oom_faults;
    }

    if (st.heap_faults == 0u && st.stalls_detected == 0u) {
        st.healthy = 1u;
    }
}

void watchdog_status(watchdog_status_t* out) {
    if (!out) return;
    *out = st;
}

void watchdog_set_policy(const watchdog_policy_t* policy) {
    if (!policy) return;
    pol = *policy;
    if (pol.stall_threshold_ticks < 100u) pol.stall_threshold_ticks = 100u;
    if (pol.vm_oom_spike_threshold < 1u) pol.vm_oom_spike_threshold = 1u;
    if (pol.heap_check_period < 8u) pol.heap_check_period = 8u;
    if (pol.heap_check_period > 4096u) pol.heap_check_period = 4096u;
    pol.auto_mitigate = pol.auto_mitigate ? 1u : 0u;
}

void watchdog_get_policy(watchdog_policy_t* out) {
    if (!out) return;
    *out = pol;
}
