/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: scheduler.c
 * Purpose: Preemptive Multitasking Scheduler, Anti-Monopolization & Context Hardening
 * =============================================================================
 */

#include "scheduler.h"
#include "string.h"
#include "multicore_scheduler.h"
#include "power.h"
#include "task_protect.h"
#include "memory_protect.h"
#include "audit.h"
#include "cgroup.h"
#include "namespace.h"
#include "io_scheduler.h"
#include "timer.h"
#include "pcoe2.h"
#include "vm.h"
#include "security.h"
#include "proc_core.h"
#include "schedlab.h"
#include "rust_bridge.h"
#include "paging.h"
#include "hw_topology.h"
#include "sched_runtime.h"
#include "serial.h"
#include "elf_loader.h"
#include "syscall.h"
#include "sandbox.h"
#include "kernel_api.h"

#define MAX_TASKS 2048
#define TASK_DEAD    SCHED_TASK_DEAD
#define TASK_READY   SCHED_TASK_READY
#define TASK_IDLE    SCHED_TASK_IDLE
#define TASK_BLOCKED SCHED_TASK_BLOCKED

#define TASK_CLASS_CPU 0
#define TASK_CLASS_IO  1
#define TASK_CLASS_NET 2
#define TASK_USER_BASE USER_VIRT_BASE
#define TASK_USER_SPAN 0x01000000u /* 16 MiB virtual address span per user task */

typedef struct {
    u32 id;
    u32 state;
    u32 runtime_ticks;
    u64 vruntime;
    u8 priority;
    u8 realtime;
    u8 task_class;
    u32 cgroup_id;
    u32 namespace_id;
    u16 starvation;
    u8 home_core;
    u8 thermal_class;
    u8 latency_target;
    u8 latency_budget;
    u8 flow_id;
    u32 cache_heat;
    u32 burst_score;
    u32 pressure;
    u32 wait_ticks;
    u32 wake_predict;
    u32 wake_frequency;
    u32 interaction_score;
    u32 queue_class;
    u32 fast_queue;
    u8 policy;
    u32 migrations;
    int has_context;
    u32 address_space;
    u32 wait_child;
    int sandbox_token;
    u8 prio_original;     /* saved priority before inheritance */
    u8 prio_inherited;    /* non-zero when priority was boosted */
    u32 blocked_by;       /* task we're blocked on (priority inheritance) */
    registers_t context;
    /* XSAVE/FXSAVE area (up to 2688 bytes): User task's AVX-512/AVX/SSE/x87 state. */
    u8 fxsave_area[2688] __attribute__((aligned(64)));
} __attribute__((aligned(64))) sched_task_t;

static sched_task_t tasks[MAX_TASKS];

#include "cpu_detect.h"

static void sched_fxsave(sched_task_t* task) {
    if (task->id == 0u) return;
    const cpu_info_t* ci = cpu_get_info();
    if (ci && ci->has_xsave) {
        u32 eax = 0xFFFFFFFFu, edx = 0xFFFFFFFFu;
        __asm__ __volatile__(
            "xsave %0"
            : "=m"(*(char*)task->fxsave_area)
            : "a"(eax), "d"(edx)
            : "memory"
        );
    } else {
        __asm__ __volatile__("fxsave %0" : "=m"(*(char*)task->fxsave_area) : : "memory");
    }
}

static void sched_fxrstor(const sched_task_t* task) {
    if (task->id == 0u) return;
    const cpu_info_t* ci = cpu_get_info();
    if (ci && ci->has_xsave) {
        u32 eax = 0xFFFFFFFFu, edx = 0xFFFFFFFFu;
        __asm__ __volatile__(
            "xrstor %0"
            :
            : "m"(*(const char*)task->fxsave_area), "a"(eax), "d"(edx)
            : "memory"
        );
    } else {
        __asm__ __volatile__("fxrstor %0" : : "m"(*(const char*)task->fxsave_area) : "memory");
    }
}

static void sched_fpu_area_reset(u8* area) {
    memset(area, 0, 2688);
    *(u16*)(area) = 0x037Fu;
    *(u32*)(area + 24) = 0x1F80u;
    *(u64*)(area + 512) = 0x7ULL;
}

/* Stack frame of the suspended kernel shell/idle context.  Points at the
   registers_t int_no field: 14 GPRs precede it in the 64-bit frame. */
u64 scheduler_idle_kernel_frame_ptr = 0u;
/* Stable source for a user-to-kernel return. The interrupt stack may contain
   the dying user frame, so the assembly stub must not use it as the source
   after the scheduler has selected task zero. */
registers_t scheduler_kernel_return_context;
static u8 kernel_context_frozen = 0u;
static u8 user_switch_requested = 0u;
static u8 kernel_resume_pending = 0u;

/* The trampoline's stack top: see kernel/kernel_entry.asm. */
extern u8 boot_stack_top;
extern u8 kernel_resume_trampoline;
static u32 current = 0;
static u32 deferred_exit_task = 0u;
static i32 deferred_exit_code = 0;
static u32 task_count = 0;
static u32 core_count = 1;
static u32 current_core = 0;
static u32 context_switch_count = 0;
static u32 sample_ticks = 0;
static u32 sample_busy = 0;
static u32 quantum_level = 2;
static u32 quantum_ticks = 2;
static u32 slice_ticks = 0;
static u32 dynamic_slice_goal = 2;
static u32 maintenance_tick = 0;
static u32 core_load[MCS_MAX_CORES];
static u32 core_temperature[MCS_MAX_CORES];
static u32 core_queue_temp[MCS_MAX_CORES];
static u32 core_last_task[MCS_MAX_CORES];
static u32 core_idle_shadow[MCS_MAX_CORES];
static u32 rebalance_cursor = 1u;
static scheduler_telemetry_t sched_tel;
static scheduler_policy_stats_t policy_stats;
static int task_index(u32 task_id);

void scheduler_capture_kernel_context(registers_t* regs) {
    if (!regs || current != 0u || regs->cs != 0x08u || regs->rip == 0u) return;
    tasks[0].context = *regs;
    scheduler_kernel_return_context = *regs;
    tasks[0].has_context = 1;
    scheduler_idle_kernel_frame_ptr = (u64)(unsigned long)regs + 120u;
    kernel_context_frozen = 1u;
}

/* The assembly return trampoline starts the shell on a fresh kernel stack.
   Drop the old saved frame so the next timer interrupt captures this new
   continuation instead of restoring a frame from before the user switch. */
void scheduler_begin_kernel_resume(void) {
    current = 0u;
    tasks[0].has_context = 0;
    scheduler_idle_kernel_frame_ptr = 0u;
    kernel_context_frozen = 0u;
    user_switch_requested = 0u;
    scheduler_kernel_return_context.rip = 0u;
    scheduler_kernel_return_context.rsp = 0u;
}

void scheduler_request_user_switch(void) {
    user_switch_requested = 1u;
    kernel_context_frozen = 0u;
}

static void scheduler_apply_user_syscall_policy(u32 task_id) {
    static const u32 allowed[] = {
        KSYSCALL_NOP, KSYSCALL_WRITE_CHAR, KSYSCALL_WRITE_STR,
        KSYSCALL_GET_TICKS, KSYSCALL_ALLOC, KSYSCALL_FREE,
        KSYSCALL_GET_TASK_ID, KSYSCALL_YIELD, KSYSCALL_VERSION,
        KSYSCALL_OPEN, KSYSCALL_CLOSE, KSYSCALL_READ, KSYSCALL_WRITE,
        KSYSCALL_SEEK, KSYSCALL_DUP, KSYSCALL_STAT, KSYSCALL_EXIT,
        KSYSCALL_WAIT, KSYSCALL_EXEC, KSYSCALL_PIPE_CREATE,
        KSYSCALL_PIPE_READ, KSYSCALL_PIPE_WRITE, KSYSCALL_PIPE_CLOSE,
        KSYSCALL_LISTEN, KSYSCALL_ACCEPT, KSYSCALL_SEND, KSYSCALL_RECV,
        KSYSCALL_SOCKET_CLOSE, KSYSCALL_SPAWN, KSYSCALL_SIGNAL,
        KSYSCALL_FORK, KSYSCALL_SOCKET_OPEN,
        KSYSCALL_GFX_INFO, KSYSCALL_GFX_MODE, KSYSCALL_GFX_CLEAR,
        KSYSCALL_GFX_RECT, KSYSCALL_GFX_TEXT, KSYSCALL_GFX_EXIT,
        KSYSCALL_GFX_BLIT, KSYSCALL_INPUT_READ,
        /* The allowlist is deny-by-default and syscall_policy_allowed() gates
           the dispatcher, so every omission is a silent -1 in ring 3.  These
           were missing: the desktop shell polled the pointer every frame and
           got a denial each time, which is why the cursor never tracked. */
        KSYSCALL_GFX_ROUND_RECT, KSYSCALL_MOUSE_READ, KSYSCALL_SHELL_EXEC,
        KSYSCALL_CLOCK_GETTIME, KSYSCALL_NANOSLEEP,
        KSYSCALL_THREAD_CREATE, KSYSCALL_SIGACTION, KSYSCALL_SIGRETURN,
        KSYSCALL_GETPRIO, KSYSCALL_SETPRIO,
        KSYSCALL_HTTP_FETCH, KSYSCALL_DRM_CREATE_BUFFER,
        KSYSCALL_DRM_PAGE_FLIP, KSYSCALL_DRM_BLIT, KSYSCALL_DRM_DESTROY_BUFFER
    };
    syscall_policy_reset(task_id);
    for (u32 nr = 0u; nr < SYSCALL_POLICY_MAX_NR; nr++) {
        (void)syscall_policy_set(task_id, nr, 0u);
    }
    for (u32 i = 0u; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        (void)syscall_policy_set(task_id, allowed[i], 1u);
    }
}

static const char* const default_process_names[] = {
    "kernel-idle", "lastos-init", "lastos-shell", "io-service", "net-service"
};

static u32 pid_state;

static u32 scheduler_pid_entropy(void) {
    u32 low, high;
    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return low ^ high ^ ((u32)(unsigned long)&pid_state >> 4u) ^ 0x4C415354u;
}

static u32 scheduler_next_pid(void) {
    u32 candidate;
    do {
        pid_state ^= pid_state << 13u;
        pid_state ^= pid_state >> 17u;
        pid_state ^= pid_state << 5u;
        candidate = 100u + (pid_state % 900000u);
    } while (candidate == 0u || task_index(candidate) >= 0);
    return candidate;
}

static u32 clamp100(u32 v) {
    return (v > 100u) ? 100u : v;
}

static u32 decay(u32 value, u32 amount) {
    return (value > amount) ? (value - amount) : 0u;
}

static int has_runnable_context(u32 idx) {
    if (idx >= task_count) {
        return 0;
    }
    return (idx == 0u || tasks[idx].has_context) ? 1 : 0;
}

static void scheduler_sync_task_to_rust(u32 idx) {
    if (idx >= task_count) return;
    if (tasks[idx].state == TASK_DEAD) return;
    rust_bridge_sched_on_task(
        tasks[idx].id,
        tasks[idx].priority,
        tasks[idx].task_class,
        (u32)(tasks[idx].vruntime & 0xFFFFFFFFu),
        tasks[idx].runtime_ticks
    );
}

static u32 ready_depth(void) {
    u32 c = 0u;
    for (u32 i = 1u; i < task_count; i++) {
        if (tasks[i].state == TASK_READY && has_runnable_context(i)) c++;
    }
    return c;
}

static sched_queue_decision_t task_queue_decision(u32 idx) {
    sched_task_model_t m;
    m.task_id = tasks[idx].id;
    m.task_class = tasks[idx].task_class;
    m.realtime = tasks[idx].realtime;
    m.thermal_class = tasks[idx].thermal_class;
    m.latency_budget = tasks[idx].latency_budget ? tasks[idx].latency_budget : tasks[idx].latency_target;
    m.wake_frequency = tasks[idx].wake_frequency;
    m.cache_heat = tasks[idx].cache_heat;
    m.interaction_score = tasks[idx].interaction_score;
    m.pressure = tasks[idx].pressure;
    return sched_runtime_classify(&m);
}

static sched_policy_t derive_policy(u32 idx, const sched_queue_decision_t* qd) {
    if (idx == 0u || tasks[idx].state == TASK_IDLE) {
        return SCHED_POLICY_IDLE;
    }
    if (tasks[idx].realtime) {
        return SCHED_POLICY_RT;
    }
    if (qd && qd->fast_queue != SCHED_FAST_NONE) {
        return SCHED_POLICY_FASTLANE;
    }
    return SCHED_POLICY_CFS;
}

static void refresh_policy_stats(void) {
    policy_stats.cfs_tasks = 0u;
    policy_stats.rt_tasks = 0u;
    policy_stats.fastlane_tasks = 0u;
    policy_stats.idle_tasks = 0u;
    for (u32 i = 0u; i < task_count; i++) {
        if (tasks[i].state == TASK_DEAD) continue;
        if (tasks[i].policy == SCHED_POLICY_RT) policy_stats.rt_tasks++;
        else if (tasks[i].policy == SCHED_POLICY_FASTLANE) policy_stats.fastlane_tasks++;
        else if (tasks[i].policy == SCHED_POLICY_IDLE) policy_stats.idle_tasks++;
        else policy_stats.cfs_tasks++;
    }
}

static u32 core_score_for_task(u32 idx, u32 core) {
    sched_queue_decision_t qd = task_queue_decision(idx);
    u32 score = core_load[core] + core_queue_temp[core] + core_temperature[core];
    score += hw_topology_core_temperature(core);
    if (tasks[idx].home_core == core) {
        u32 affinity_bonus = (tasks[idx].cache_heat / 2u) + 16u;
        if (score > affinity_bonus) score -= affinity_bonus;
        else score = 0u;
    }
    if (core_last_task[core] == tasks[idx].id && score > 12u) {
        score -= 12u;
    }
    if (tasks[idx].latency_target >= 2u && core_idle_shadow[core] && score > 16u) {
        score -= 16u;
    }
    if (tasks[idx].thermal_class >= 2u) {
        score += core_temperature[core] / 2u;
        score += hw_topology_core_temperature(core) / 3u;
    }
    if (qd.migration_limit && tasks[idx].home_core != core) {
        score += 36u;
    }
    if (qd.fast_queue != SCHED_FAST_NONE && core_idle_shadow[core] && score > 20u) {
        score -= 20u;
    }
    return score;
}

static u32 select_core_for_task(u32 idx) {
    if (core_count <= 1u || idx >= task_count) {
        return 0u;
    }
    u32 best = tasks[idx].home_core;
    if (best >= core_count) {
        best = 0u;
    }
    u32 best_score = core_score_for_task(idx, best);
    for (u32 c = 0u; c < core_count; c++) {
        u32 s = core_score_for_task(idx, c);
        if (s < best_score) {
            best = c;
            best_score = s;
        }
    }
    {
        u32 rust_core = best;
        if (rust_bridge_sched_pick_core(tasks[idx].id, best, &rust_core) == 0 && rust_core < core_count) {
            best = rust_core;
        }
    }
    return best;
}

static void scheduler_update_task_signals(u32 ran_idx, u32 busy) {
    u32 runq = ready_depth();
    u32 total_pressure = 0u;
    u32 hot_threads = 0u;
    u32 speculative = 0u;
    u32 realtime = 0u;
    u32 idle_cores = 0u;

    for (u32 c = 0u; c < core_count; c++) {
        if (core_idle_shadow[c]) idle_cores++;
    }
    sched_runtime_begin_sample(core_count, idle_cores);

    for (u32 i = 1u; i < task_count; i++) {
        sched_queue_decision_t qd;
        if (tasks[i].state != TASK_READY) {
            tasks[i].wait_ticks = 0u;
            tasks[i].pressure = decay(tasks[i].pressure, 3u);
            tasks[i].burst_score = decay(tasks[i].burst_score, 2u);
            continue;
        }
        if (i == ran_idx && busy) {
            tasks[i].wait_ticks = 0u;
            tasks[i].cache_heat = clamp100(tasks[i].cache_heat + 4u);
            tasks[i].burst_score = clamp100(tasks[i].burst_score + ((slice_ticks <= 1u) ? 5u : 2u));
            tasks[i].pressure = clamp100((tasks[i].runtime_ticks / 4u) + runq + tasks[i].cache_heat / 3u);
        } else {
            tasks[i].wait_ticks = clamp100(tasks[i].wait_ticks + 1u);
            tasks[i].cache_heat = decay(tasks[i].cache_heat, 1u);
            tasks[i].burst_score = decay(tasks[i].burst_score, 1u);
            tasks[i].pressure = clamp100(tasks[i].pressure + (tasks[i].wait_ticks / 8u));
        }
        tasks[i].wake_frequency = clamp100((tasks[i].wake_frequency * 7u + tasks[i].wake_predict + (i == ran_idx ? 24u : 0u)) / 8u);
        if (tasks[i].latency_budget == 0u) {
            tasks[i].latency_budget = tasks[i].latency_target ? tasks[i].latency_target : 4u;
        }
        if (i == ran_idx && busy) {
            tasks[i].interaction_score = clamp100(tasks[i].interaction_score + ((tasks[i].latency_budget <= 2u) ? 4u : 1u));
        } else {
            tasks[i].interaction_score = decay(tasks[i].interaction_score, 1u);
        }
        tasks[i].wake_predict = clamp100((tasks[i].wait_ticks * 2u) + tasks[i].burst_score + (tasks[i].latency_target * 8u));
        qd = task_queue_decision(i);
        tasks[i].queue_class = (u32)qd.temp_queue;
        tasks[i].fast_queue = (u32)qd.fast_queue;
        tasks[i].policy = (u8)derive_policy(i, &qd);
        sched_runtime_note_task(&qd, tasks[i].wait_ticks, tasks[i].pressure);
        rust_bridge_sched_task_signals(
            tasks[i].id,
            tasks[i].home_core,
            tasks[i].cache_heat,
            tasks[i].burst_score,
            tasks[i].pressure,
            tasks[i].wait_ticks,
            tasks[i].latency_target,
            tasks[i].thermal_class,
            tasks[i].flow_id,
            tasks[i].wake_predict
        );
        total_pressure += tasks[i].pressure;
        if (tasks[i].cache_heat > 70u || tasks[i].pressure > 80u) hot_threads++;
        if (tasks[i].wake_predict > 60u) speculative++;
        if (tasks[i].realtime) realtime++;
    }

    sched_tel.task_pressure = task_count > 1u ? clamp100(total_pressure / (task_count - 1u)) : 0u;
    sched_tel.hot_thread_isolations = hot_threads;
    sched_tel.speculative_wakes = speculative;
    sched_tel.realtime_islands = realtime;
    sched_runtime_finish_sample(sched_tel.task_pressure);
    refresh_policy_stats();
}

static void scheduler_update_core_signals(u32 core, u32 busy) {
    if (core >= MCS_MAX_CORES) {
        return;
    }
    if (busy) {
        u32 hw_temp = hw_topology_core_temperature(core);
        core_temperature[core] = clamp100(core_temperature[core] + 2u + (hw_temp > 70u ? 2u : 0u));
        core_queue_temp[core] = clamp100(core_queue_temp[core] + ready_depth());
        core_idle_shadow[core] = 0u;
    } else {
        core_temperature[core] = decay(core_temperature[core], 1u);
        core_queue_temp[core] = decay(core_queue_temp[core], 2u);
        core_idle_shadow[core] = 1u;
    }
    core_last_task[core] = tasks[current].id;
    hw_topology_account_core(core, busy, ready_depth());
}

static void scheduler_refresh_telemetry(void) {
    u32 heat = 0u, queue = 0u, shadow = 0u, migr = 0u, wake = 0u, flow = 0u;
    sched_runtime_snapshot_t rt;
    for (u32 c = 0u; c < core_count; c++) {
        heat += core_temperature[c];
        queue += core_queue_temp[c];
        if (core_idle_shadow[c]) shadow++;
    }
    for (u32 i = 1u; i < task_count; i++) {
        heat += tasks[i].cache_heat;
        migr += tasks[i].migrations;
        wake += tasks[i].wake_predict;
        if (tasks[i].flow_id == tasks[current].flow_id) flow += 8u;
    }
    u32 denom = core_count + ((task_count > 1u) ? (task_count - 1u) : 1u);
    sched_tel.thermal_class_score = clamp100(heat / (denom ? denom : 1u));
    sched_tel.cache_heat_score = sched_tel.thermal_class_score;
    sched_tel.queue_temperature = core_count ? clamp100(queue / core_count) : 0u;
    sched_tel.shadow_idle_cores = shadow;
    sched_tel.migration_predictions = migr;
    sched_tel.cross_core_wake_predictions = clamp100(wake / ((task_count > 1u) ? (task_count - 1u) : 1u));
    sched_tel.flow_placement_score = clamp100(flow);
    sched_tel.pulse_path_score = clamp100((100u - sched_tel.queue_temperature) + (sched_tel.shadow_idle_cores * 5u));
    sched_tel.latency_core_score = clamp100((100u - sched_tel.task_pressure) + sched_tel.shadow_idle_cores);
    sched_tel.adaptive_quantum = dynamic_slice_goal;
    sched_tel.burst_prediction_score = sched_tel.cross_core_wake_predictions;
    sched_tel.microburst_balance_score = clamp100(100u - sched_tel.queue_temperature + ready_depth());
    sched_tel.energy_latency_score = clamp100((100u - sched_tel.thermal_class_score + sched_tel.latency_core_score) / 2u);
    sched_tel.desktop_fastlane_score = clamp100(100u - tasks[current].wait_ticks + (tasks[current].latency_target * 10u));
    if (hw_topology_numa_nodes() > 1u) {
        u32 same = 0u, total = 0u;
        u32 cur_node = hw_topology_core_numa_node(current_core);
        for (u32 i = 1u; i < task_count; i++) {
            if (tasks[i].state != TASK_READY) continue;
            total++;
            if (hw_topology_core_numa_node(tasks[i].home_core) == cur_node) same++;
        }
        sched_tel.numa_affinity_score = total ? clamp100((same * 100u) / total) : 100u;
    } else {
        sched_tel.numa_affinity_score = 100u;
    }
    sched_runtime_snapshot(&rt);
    sched_tel.hot_queue_depth = rt.hot_depth;
    sched_tel.warm_queue_depth = rt.warm_depth;
    sched_tel.cold_queue_depth = rt.cold_depth;
    sched_tel.render_queue_depth = rt.render_depth;
    sched_tel.cursor_queue_depth = rt.cursor_depth;
    sched_tel.audio_queue_depth = rt.audio_depth;
    sched_tel.input_queue_depth = rt.input_depth;
    sched_tel.latency_spikes = rt.latency_spikes;
    sched_tel.idle_ratio = rt.idle_ratio;
    sched_tel.fast_path_hits = rt.fast_path_hits;
    sched_tel.migration_limited = rt.migration_limited;
}

static u32 level_to_ticks(u32 level) {
    if (level <= 1u) {
        return 1u;
    }
    if (level >= 3u) {
        return 4u;
    }
    return 2u;
}

static int task_index(u32 task_id) {
    for (u32 i = 0; i < task_count; i++) {
        if (tasks[i].id == task_id) {
            return (int)i;
        }
    }
    return -1;
}

static u64 effective_vruntime(const sched_task_t* t) {
    io_stats_t io;
    io_scheduler_stats(&io);
    u64 v = t->vruntime;
    if (io.queue_depth > 8 && t->task_class == TASK_CLASS_IO) {
        if (v > 32) v -= 32;
    }
    if (io.net_packets > 200 && t->task_class == TASK_CLASS_NET) {
        if (v > 32) v -= 32;
    }
    u32 preempt_hint = pcoe2_scheduler_preempt_hint();
    if (preempt_hint >= 3u && t->task_class == TASK_CLASS_NET) {
        if (v > 20) v -= 20;
    } else if (preempt_hint == 2u && t->task_class == TASK_CLASS_IO) {
        if (v > 12) v -= 12;
    }
    if (t->starvation > 0u) {
        u64 boost = (u64)(t->starvation / 4u);
        if (v > boost) {
            v -= boost;
        } else {
            v = 0u;
        }
    }
    return v;
}

static u32 prio_weight(u8 p) {
    if (p > 39) {
        p = 39;
    }
    return 40u - (u32)p;
}

static u64 apply_score_delta(u64 score, i32 delta) {
    if (delta < 0) {
        u64 d = (u64)(-delta);
        return (score > d) ? (score - d) : 0u;
    }
    return score + (u64)delta;
}

static u64 advised_vruntime(u32 idx, u32 current_ns, u32 runq_depth, u32 pressure_hint) {
    i32 delta = 0;
    sched_queue_decision_t qd = task_queue_decision(idx);
    u64 v = effective_vruntime(&tasks[idx]);

    if (tasks[idx].namespace_id == current_ns) {
        if (v > 6u) v -= 6u;
    } else if (current_ns != 0u) {
        v += 6u;
    }

    if (tasks[idx].home_core == current_core && v > 8u) {
        v -= 8u;
    }
    if (tasks[idx].latency_target >= 2u) {
        u32 boost = 8u + tasks[idx].wake_predict / 8u;
        if (v > boost) v -= boost;
    }
    if (tasks[idx].burst_score > 40u) {
        if (v > 10u) v -= 10u;
    }
    if (tasks[idx].pressure > 80u) {
        v += 16u;
    }
    if (tasks[idx].thermal_class >= 2u && tasks[idx].cache_heat > 75u) {
        v += 12u;
    }
    if (tasks[idx].flow_id == tasks[current].flow_id && tasks[idx].flow_id != 0u && v > 4u) {
        v -= 4u;
    }
    if (qd.preempt_boost > 0u) {
        if (v > qd.preempt_boost) v -= qd.preempt_boost;
        else v = 0u;
    }
    if (qd.fast_queue != SCHED_FAST_NONE && v > qd.wake_cost) {
        v -= qd.wake_cost;
    }
    if (qd.temp_queue == SCHED_TEMP_COLD && runq_depth > 2u) {
        v += 18u;
    }

    if (rust_bridge_sched_advise(
            tasks[idx].id,
            current_core,
            current_ns,
            runq_depth,
            pressure_hint,
            tasks[idx].runtime_ticks,
            &delta) == 0) {
        v = apply_score_delta(v, delta);
    }
    return v;
}

static int eligible(u32 idx, u32 now_tick) {
    if (tasks[idx].state != TASK_READY) {
        return 0;
    }
    if (!has_runnable_context(idx)) {
        return 0;
    }
    if (!cgroup_cpu_allow(tasks[idx].id, now_tick)) {
        return 0;
    }
    return 1;
}

static u32 pick_next_task(u32 now_tick) {
    u32 rust_pick = 0u;
    u32 runq_depth = ready_depth();
    u32 pressure_hint = pcoe2_scheduler_preempt_hint();
    u32 current_ns = tasks[current].namespace_id;
    if (rust_bridge_sched_pick(current_core, &rust_pick) == 0) {
        int ridx = task_index(rust_pick);
        if (ridx > 0 && eligible((u32)ridx, now_tick)) {
            return (u32)ridx;
        }
    }

    u32 best = 0;
    int have = 0;

    /* Linux-like policy order: RT -> fast interactive lanes -> fair CFS -> idle.
       Among RT tasks with equal priority, prefer the runnable user (ring-3)
       process over a kernel service task; otherwise the static kernel tasks
       that stay READY forever would starve every spawned userland binary.
       Equal-priority RT peers then rotate by wait_ticks: picking the lowest
       task slot forever kept desktop_shell running and starved every later
       ring-3 process, including Linux-compat executables. */
    for (u32 i = 1; i < task_count; i++) {
        if (!eligible(i, now_tick)) {
            continue;
        }
        if (tasks[i].policy != SCHED_POLICY_RT) {
            continue;
        }
        int better = !have ||
                     tasks[i].priority < tasks[best].priority ||
                     (tasks[i].priority == tasks[best].priority &&
                      tasks[i].address_space != PAGING_SPACE_KERNEL &&
                      tasks[best].address_space == PAGING_SPACE_KERNEL) ||
                     (tasks[i].priority == tasks[best].priority &&
                      (tasks[i].address_space != PAGING_SPACE_KERNEL) ==
                          (tasks[best].address_space != PAGING_SPACE_KERNEL) &&
                      tasks[i].wait_ticks > tasks[best].wait_ticks);
        if (better) {
            best = i;
            have = 1;
        }
    }
    if (have) {
        return best;
    }

    for (u32 i = 1; i < task_count; i++) {
        if (!eligible(i, now_tick) || tasks[i].policy != SCHED_POLICY_FASTLANE) {
            continue;
        }
        u64 cand = advised_vruntime(i, current_ns, runq_depth, pressure_hint);
        u64 curv = advised_vruntime(best, current_ns, runq_depth, pressure_hint);
        if (!have || cand < curv) {
            best = i;
            have = 1;
        }
    }
    if (have) {
        return best;
    }

    for (u32 i = 1; i < task_count; i++) {
        if (!eligible(i, now_tick) || tasks[i].policy != SCHED_POLICY_CFS) {
            continue;
        }
        u64 cand = advised_vruntime(i, current_ns, runq_depth, pressure_hint);
        u64 curv = advised_vruntime(best, current_ns, runq_depth, pressure_hint);
        if (!have || cand < curv) {
            best = i;
            have = 1;
        }
    }

    if (have) {
        return best;
    }
    return 0;
}

static u32 pick_runnable_peer(u32 now_tick, u32 excluded) {
    for (u32 i = 1u; i < task_count; i++) {
        if (i == excluded) continue;
        if (eligible(i, now_tick)) return i;
    }
    return 0u;
}

static void refresh_starvation(u32 next_idx) {
    for (u32 i = 1; i < task_count; i++) {
        if (tasks[i].state != TASK_READY) {
            tasks[i].starvation = 0u;
            continue;
        }
        if (i == next_idx) {
            if (tasks[i].starvation > 8u) tasks[i].starvation -= 8u;
            else tasks[i].starvation = 0u;
            continue;
        }
        if (tasks[i].starvation < 1000u) {
            tasks[i].starvation += 1u;
        }
    }
}

static void normalize_vruntime(void) {
    u64 min_v = ~0ull;
    int have = 0;
    for (u32 i = 1; i < task_count; i++) {
        if (tasks[i].state != TASK_READY) continue;
        if (!have || tasks[i].vruntime < min_v) {
            min_v = tasks[i].vruntime;
            have = 1;
        }
    }
    if (!have || min_v < 1024ull) return;
    for (u32 i = 1; i < task_count; i++) {
        if (tasks[i].vruntime >= min_v) tasks[i].vruntime -= min_v;
        else tasks[i].vruntime = 0;
    }
}

static void compact_dead_tasks(void) {
    if (task_count <= 1u) return;
    u32 write = 1u;
    for (u32 read = 1u; read < task_count; read++) {
        if (tasks[read].state == TASK_DEAD) continue;
        if (write != read) {
            tasks[write] = tasks[read];
            if (current == read) current = write;
        }
        write++;
    }
    for (u32 i = write; i < task_count; i++) {
        if (tasks[i].sandbox_token >= 0) sandbox_close(tasks[i].sandbox_token);
        tasks[i].state = TASK_DEAD;
        tasks[i].id = 0u;
        tasks[i].runtime_ticks = 0u;
        tasks[i].vruntime = 0u;
        tasks[i].priority = 39u;
        tasks[i].realtime = 0u;
        tasks[i].task_class = TASK_CLASS_CPU;
        tasks[i].cgroup_id = 0u;
        tasks[i].namespace_id = 0u;
        tasks[i].starvation = 0u;
        tasks[i].home_core = 0u;
        tasks[i].thermal_class = 0u;
        tasks[i].latency_target = 0u;
        tasks[i].latency_budget = 0u;
        tasks[i].flow_id = 0u;
        tasks[i].cache_heat = 0u;
        tasks[i].burst_score = 0u;
        tasks[i].pressure = 0u;
        tasks[i].wait_ticks = 0u;
        tasks[i].wake_predict = 0u;
        tasks[i].wake_frequency = 0u;
        tasks[i].interaction_score = 0u;
        tasks[i].queue_class = SCHED_TEMP_COLD;
        tasks[i].fast_queue = SCHED_FAST_NONE;
        tasks[i].policy = SCHED_POLICY_CFS;
        tasks[i].migrations = 0u;
        tasks[i].has_context = 0;
        tasks[i].sandbox_token = -1;
    }
    task_count = write;
    if (current >= task_count) current = 0u;
}

static void rebalance_affinity(void) {
    if (core_count <= 1u || task_count <= 2u) {
        return;
    }

    u32 min_core = 0u, max_core = 0u;
    for (u32 c = 1u; c < core_count; c++) {
        if (core_load[c] < core_load[min_core]) min_core = c;
        if (core_load[c] > core_load[max_core]) max_core = c;
    }
    if (core_load[max_core] <= core_load[min_core] + 8u) {
        return;
    }

    for (u32 n = 0u; n < task_count; n++) {
        u32 i = (rebalance_cursor + n) % task_count;
        if (i == 0u) continue;
        if (tasks[i].state != TASK_READY || tasks[i].realtime) continue;
        if (tasks[i].home_core == max_core) {
            tasks[i].home_core = (u8)min_core;
            rebalance_cursor = (i + 1u) % task_count;
            break;
        }
    }
}

void scheduler_init(void) {
    /* Memory & Scheduler Hardening: Initialize subsystem and execute 8-vector defense verification */
            task_count = 5;
    pid_state = scheduler_pid_entropy();
    if (pid_state == 0u) pid_state = 0xA53C9E17u;

    for (u32 i = 0; i < task_count; i++) {
        tasks[i].id = (i == 0u) ? 0u : scheduler_next_pid();
        tasks[i].state = (i == 0) ? TASK_IDLE : TASK_READY;
        tasks[i].runtime_ticks = 0;
        tasks[i].vruntime = 0;
        tasks[i].priority = (i == 0) ? 0 : 20;
        tasks[i].realtime = 0;
        tasks[i].task_class = (i == 2) ? TASK_CLASS_IO : ((i == 3) ? TASK_CLASS_NET : TASK_CLASS_CPU);
        tasks[i].cgroup_id = 1;
        tasks[i].namespace_id = 1;
        tasks[i].starvation = 0u;
        tasks[i].home_core = 0u;
        tasks[i].thermal_class = (i == 1u) ? 1u : ((i == 4u) ? 2u : 0u);
        tasks[i].latency_target = (i == 1u) ? 3u : ((i == 2u || i == 3u) ? 2u : 1u);
        tasks[i].latency_budget = (i == 1u) ? 1u : ((i == 2u) ? 2u : ((i == 3u) ? 3u : 5u));
        tasks[i].flow_id = (u8)((i == 2u || i == 3u) ? 2u : 1u);
        tasks[i].cache_heat = 0u;
        tasks[i].burst_score = 0u;
        tasks[i].pressure = 0u;
        tasks[i].wait_ticks = 0u;
        tasks[i].wake_predict = 0u;
        tasks[i].wake_frequency = 0u;
        tasks[i].interaction_score = (i == 1u) ? 80u : ((i == 2u) ? 50u : 20u);
        tasks[i].queue_class = SCHED_TEMP_COLD;
        tasks[i].fast_queue = SCHED_FAST_NONE;
        tasks[i].policy = (i == 0u) ? SCHED_POLICY_IDLE : SCHED_POLICY_CFS;
        tasks[i].migrations = 0u;
        tasks[i].has_context = 0;
        tasks[i].sandbox_token = -1;
    }

    for (u32 i = 1; i < task_count; i++) {
        u64 start = TASK_USER_BASE + ((u64)i * TASK_USER_SPAN);
        u64 end = start + TASK_USER_SPAN;
        task_protect_set_range(tasks[i].id, start, end, TASK_PRIV_USER);
        memory_protect_set_task_stack(tasks[i].id, end - 0x8000u, end - 0x1000u);
        security_set_task_caps(tasks[i].id, SEC_CAP_NET_ADMIN);
        (void)vm_register_task_space(tasks[i].id, start, end);
        proc_core_register_task(tasks[i].id, 1u);
    }
    proc_core_register_task(0u, 0u);
    security_set_task_caps(1u, SEC_CAP_MODULE_LOAD | SEC_CAP_FS_ADMIN | SEC_CAP_NET_ADMIN);

    task_protect_set_range(0, 0x00000000, 0x00400000, TASK_PRIV_KERNEL);
    cgroup_bind_task(0, 1);
    namespace_bind_task(0, 1);
    for (u32 i = 1; i < task_count; i++) {
        cgroup_bind_task(tasks[i].id, 1);
        namespace_bind_task(tasks[i].id, 1);
    }

    current = 0;
    context_switch_count = 0;
    sample_ticks = 0;
    sample_busy = 0;
    quantum_level = 2;
    quantum_ticks = level_to_ticks(quantum_level);
    slice_ticks = 0;
    maintenance_tick = 0;
    scheduler_set_core_count(1);
    for (u32 c = 0; c < MCS_MAX_CORES; c++) {
        core_load[c] = 0u;
        core_temperature[c] = 0u;
        core_queue_temp[c] = 0u;
        core_last_task[c] = 0u;
        core_idle_shadow[c] = 1u;
    }
    scheduler_telemetry_t zero = {0};
    sched_tel = zero;
    scheduler_policy_stats_t pzero = {0};
    policy_stats = pzero;
    sched_runtime_init();
    rebalance_cursor = 1u;
    for (u32 i = 0u; i < task_count; i++) {
        scheduler_sync_task_to_rust(i);
    }
    schedlab_init();
}

void scheduler_set_core_count(u32 count) {
    if (count == 0) {
        count = 1;
    }
    if (count > MCS_MAX_CORES) {
        count = MCS_MAX_CORES;
    }

    core_count = count;
    mcs_init(core_count);
    for (u32 c = 0; c < MCS_MAX_CORES; c++) {
        core_load[c] = 0u;
        core_temperature[c] = 0u;
        core_queue_temp[c] = 0u;
        core_last_task[c] = 0u;
        core_idle_shadow[c] = (c < core_count) ? 1u : 0u;
    }
    rust_bridge_sched_tick(0u, 1u);
}

/* The SYSCALL fast path has no interrupt frame to rewrite.  Defer teardown to
   the next timer tick so sysret returns while the old stack and address space
   still exist; the tick then switches directly to another runnable task. */
int scheduler_defer_user_exit(u32 task_id, i32 exit_code) {
    int idx = task_index(task_id);
    if (idx <= 0 || current != (u32)idx || deferred_exit_task != 0u) return -1;
    deferred_exit_task = task_id;
    deferred_exit_code = exit_code;
    tasks[idx].state = TASK_DEAD;
    return 0;
}

/* This handler may rewrite the active interrupt frame and select a different
   address space before returning to the assembly stub. Its C stack is not a
   normal call boundary, so a compiler canary cannot survive that transition. */
__attribute__((no_stack_protector))
void scheduler_tick(registers_t* regs) {
    if (task_count == 0) {
        return;
    }
    if (deferred_exit_task != 0u) {
        u32 exiting = deferred_exit_task;
        i32 code = deferred_exit_code;
        deferred_exit_task = 0u;
        (void)scheduler_exit_task(exiting, code);
        if (regs) scheduler_schedule_from_irq(regs);
        else scheduler_schedule();
        return;
    }

    if (current != 0u) {
        u32 current_id = tasks[current].id;
        proc_signal_t sig;
        /* Check for pending signals.  If a user-space handler is registered,
         * invoke it by rewriting the user frame to call the handler with
         * a sigreturn trampoline.  Otherwise, use the default action. */
        if (proc_core_signal_poll(current_id, &sig) == 0) {
            /* Convert internal signal enum to 1-based slot for lookup. */
            u32 sig_slot = proc_signal_to_slot(sig);
            u32 handler = sig_slot ? proc_core_sigaction_get(current_id, sig_slot) : 0u;
            if (handler != 0u && handler >= USER_VIRT_BASE && handler < USER_VIRT_LIMIT &&
                sig != PROC_SIG_KILL && sig != PROC_SIG_TERM) {
                /* ── User-space signal delivery ──────────────────────
                 * Rewrite the interrupted user frame to call the handler.
                 * Save the original context on the user stack so sigreturn
                 * can restore it. */
                u64 saved_ctx = tasks[current].context.rsp - sizeof(registers_t);
                if (saved_ctx >= USER_VIRT_BASE && saved_ctx < USER_VIRT_LIMIT) {
                    if (kcopy_to_user(saved_ctx, &tasks[current].context,
                                      sizeof(registers_t)) == 0) {
                        tasks[current].context.rip = handler;
                        tasks[current].context.rsp = saved_ctx;
                        /* Pass sig number as arg (rbx). */
                        tasks[current].context.rbx = (u32)sig;
                    }
                }
            } else {
                /* Default action: TERM/KILL → exit. */
                if (sig == PROC_SIG_KILL || sig == PROC_SIG_TERM) {
                    (void)scheduler_exit_task(current_id,
                        (sig == PROC_SIG_KILL) ? 137 : 143);
                    scheduler_schedule_from_irq(regs);
                    return;
                }
                /* Other signals with no handler are silently ignored
                 * (POSIX default: USR1/USR2 = ignore). */
            }
        }
    }

    current_core = select_core_for_task(current);
    if (current_core >= MCS_MAX_CORES) current_core = 0u;
    if (core_load[current_core] < 1000000u) {
        core_load[current_core]++;
    }
    rust_bridge_sched_tick(current_core, 1u);
    u32 now_tick = timer_ticks();

    tasks[current].runtime_ticks++;
    if (current != 0) {
        u32 w = prio_weight(tasks[current].priority);
        tasks[current].vruntime += (1024u / (w ? w : 1u));
        (void)cgroup_cpu_allow(tasks[current].id, now_tick);
        scheduler_sync_task_to_rust(current);
    }

    int busy = (tasks[current].state != TASK_IDLE);
    mcs_account_tick(current_core, busy);
    scheduler_update_task_signals(current, busy ? 1u : 0u);
    scheduler_update_core_signals(current_core, busy ? 1u : 0u);
    io_scheduler_tick(now_tick);

    sample_ticks++;
    if (busy) {
        sample_busy++;
    }

    if (sample_ticks >= 200) {
        u32 busy_percent = (sample_busy * 100) / sample_ticks;
        power_on_scheduler_sample(busy_percent);
        sample_ticks = 0;
        sample_busy = 0;
    }

    /* LastGUI performs long, non-reentrant draw operations from user space.
       Keep timer ticks accounting-only while a live user task is running;
       cooperative yield points still perform full context switches.  This
       avoids rewriting an arbitrary graphics call's frame while preserving
       independent process execution at explicit scheduling points. */
    if (current != 0u && tasks[current].state != TASK_DEAD &&
        tasks[current].state != TASK_BLOCKED) {
        scheduler_refresh_telemetry();
        schedlab_tick(tasks[current].id, current_core, busy ? 1u : 0u,
                      ready_depth(), dynamic_slice_goal, quantum_level);
        return;
    }

    slice_ticks++;
    {
        u32 hint = pcoe2_scheduler_preempt_hint();
        if (hint >= 3u) dynamic_slice_goal = 1u;
        else if (hint == 2u) dynamic_slice_goal = quantum_ticks;
        else dynamic_slice_goal = quantum_ticks + 1u;
        if (dynamic_slice_goal > 6u) dynamic_slice_goal = 6u;
    }
    /* Once the shell's kernel return frame is captured, timer interrupts
       that arrive while a user task is running must not overwrite it with
       the interrupted ring-3 frame. That frame is the only safe return path
       after exec/exit switches back to the kernel task. */
    if (regs) {
        if (current == 0u) {
            if (!kernel_context_frozen && regs->cs == 0x08u) {
                tasks[0].context = *regs;
                tasks[0].has_context = 1;
            }
        } else if (regs->cs == 0x1Bu) {
            tasks[current].context = *regs;
            tasks[current].has_context = 1;
        }
    }

    if (regs && current != 0u && regs->cs == 0x1Bu) {
        sched_fxsave(&tasks[current]);
    }

    if (slice_ticks >= dynamic_slice_goal || current == 0) {
        slice_ticks = 0;
        scheduler_schedule_from_irq(regs);
    }

    scheduler_refresh_telemetry();
    schedlab_tick(tasks[current].id, current_core, busy ? 1u : 0u, ready_depth(), dynamic_slice_goal, quantum_level);
}

/* This path rewrites the interrupted frame while switching address spaces;
   a compiler canary is not meaningful across that deliberate frame change. */
__attribute__((no_stack_protector))
void scheduler_schedule_from_irq(registers_t* regs) {
    if (!regs || task_count == 0) {
        return;
    }
    /* Scheduler Hardening: Validate context frame registers of current task */

    /* During boot the kernel is still constructing the user tasks.  Do not let
       a timer tick preempt that work into a half-loaded ELF; the initial user
       transition happens only after kmain or the console explicitly requests
       it and the kernel continuation has been captured. */
    if (current == 0u && regs->cs == 0x08u &&
        !user_switch_requested && !kernel_resume_pending) {
        return;
    }

    if (current == 0u && regs->cs == 0x08u && user_switch_requested) {
        scheduler_capture_kernel_context(regs);
        user_switch_requested = 0u;
    }

    if (current == 0u && regs->cs == 0x08u && regs->rip != 0u && !kernel_context_frozen) {
        scheduler_idle_kernel_frame_ptr = (u64)(unsigned long)regs + 120u;
    }

    if (kernel_resume_pending) {
        /* A killed ring-3 task left its frame on a torn-down stack.  Return
           to the fresh trampoline (see kernel_entry.asm) instead of idle's
           stale kernel frame. */
        kernel_resume_pending = 0u;
        tasks[0].has_context = 1u;
        tasks[0].context.rsp = (u64)(uintptr_t)&boot_stack_top;
        tasks[0].context.rip = (u64)(uintptr_t)&kernel_resume_trampoline;
        tasks[0].context.cs = 0x08u;
        tasks[0].context.ss = 0x10u;
        tasks[0].context.rflags = 0x202u;
        kernel_context_frozen = 1u;
    }

    if (current != 0u && tasks[current].has_context) {
        /* For a ring-3 interrupt frame, the interrupt rsp is the ring-3
           stack restored by iret (the saved rsp register is not used). */
        u64 stack_pointer = regs->rsp;
        if (!memory_protect_validate_task_stack(tasks[current].id, stack_pointer)) {
            audit_log(AUDIT_EVT_STACK_OVERFLOW, (u32)stack_pointer, (u32)regs->rip);
            regs->rsp = USER_VIRT_LIMIT - 0x1000u;
        }

        /* The i386-era monitor validates against 32-bit task ranges that no
           longer match a high-half user stack; only the 64-bit stack range
           check is meaningful here. */
        if (!memory_protect_validate_task_stack(tasks[current].id, stack_pointer)) {
            audit_log(AUDIT_EVT_STACK_OVERFLOW, (u32)stack_pointer, (u32)regs->rip);
            regs->rsp = USER_VIRT_LIMIT - 0x1000u;
        }
    }

    if (!task_protect_validate_context(regs)) {
        audit_log(AUDIT_EVT_TASK_VIOLATION, regs->cs, (u32)regs->rip);
        regs->cs = 0x08;
        regs->rip = KERNEL_VIRT_BASE;
    }

    if (regs) {
        if (current == 0u) {
            if (!kernel_context_frozen && regs->cs == 0x08u) {
                tasks[0].context = *regs;
                tasks[0].has_context = 1;
            }
        } else if (regs->cs == 0x1Bu) {
            tasks[current].context = *regs;
            tasks[current].has_context = 1;
        }
    }

    if (regs && current != 0u && regs->cs == 0x1Bu) {
        sched_fxsave(&tasks[current]);
    }

    u32 previous = current;
    u32 next = pick_next_task(timer_ticks());
    if (regs->cs == 0x1Bu && next == previous) {
        u32 peer = pick_runnable_peer(timer_ticks(), previous);
        if (peer != 0u) next = peer;
    }
    if (next != 0 && !cgroup_cpu_allow(tasks[next].id, timer_ticks())) {
        next = 0;
    }
    /* An interrupt that arrived from ring 3 cannot return through the idle
       task unless a valid kernel continuation is being selected.  Quota
       accounting may temporarily make every ready task ineligible; keep the
       interrupted user frame in that case instead of switching CR3 to the
       kernel and iret-ing into the old user stack. */
    if (next == 0u && previous != 0u && regs->cs == 0x1Bu &&
        tasks[previous].state != TASK_DEAD &&
        tasks[previous].state != TASK_BLOCKED) {
        next = previous;
    }
    refresh_starvation(next);
    if (next != current) {
        u32 target_core = select_core_for_task(next);
        context_switch_count++;
        if (target_core != tasks[next].home_core) {
            tasks[next].migrations++;
        }
        current_core = target_core;
        schedlab_on_switch(tasks[current].id, tasks[next].id, current_core);
        tasks[next].home_core = (u8)current_core;
        slice_ticks = 0;
    }
    current = next;
    (void)paging_space_switch(tasks[current].address_space);
    scheduler_sync_task_to_rust(current);

    /* When the scheduler keeps the same task, the interrupt frame already
       contains the newest rip/rsp pair.  Replacing it with the cached
       initial context would rewind the stack and make a later ret jump to 0.
       A cached context is needed only for a real task switch. */
    if (tasks[current].has_context && current != previous) {
        *regs = tasks[current].context;
        if (current == 0u && regs->cs == 0x08u) {
            scheduler_kernel_return_context = *regs;
        }
        if (!task_protect_validate_context(regs)) {
            audit_log(AUDIT_EVT_TASK_VIOLATION, regs->cs, (u32)regs->rip);
            regs->cs = 0x08;
            regs->rip = KERNEL_VIRT_BASE;
        }
    }

    if (tasks[current].id != 0u) {
        sched_fxrstor(&tasks[current]);
    }

    maintenance_tick++;
    if (maintenance_tick >= 256u) {
        maintenance_tick = 0u;
        normalize_vruntime();
        rebalance_affinity();
        compact_dead_tasks();
        for (u32 c = 0; c < core_count; c++) {
            core_load[c] = (core_load[c] * 3u) / 4u;
        }
    }
}

void scheduler_schedule(void) {
    if (task_count == 0) {
        return;
    }
    u32 next = pick_next_task(timer_ticks());
    if (next != 0 && !cgroup_cpu_allow(tasks[next].id, timer_ticks())) {
        next = 0;
    }
    refresh_starvation(next);
    if (next != current) {
        u32 target_core = select_core_for_task(next);
        context_switch_count++;
        if (target_core != tasks[next].home_core) {
            tasks[next].migrations++;
        }
        current_core = target_core;
        schedlab_on_switch(tasks[current].id, tasks[next].id, current_core);
        tasks[next].home_core = (u8)current_core;
        slice_ticks = 0;
    }
    current = next;
    (void)paging_space_switch(tasks[current].address_space);
    if (tasks[current].id != 0u) {
        sched_fxrstor(&tasks[current]);
    }
    scheduler_sync_task_to_rust(current);
}

u32 scheduler_current_task(void) {
    if (task_count == 0) {
        return 0;
    }
    return tasks[current].id;
}

int scheduler_create_user_task(const char* name, u32* out_task_id) {
    if (!out_task_id || task_count < 2u) return -1;
    u32 idx = task_count;
    u32 appended = 0u;
    for (u32 i = 1u; i < task_count; i++) {
        if (tasks[i].state == TASK_DEAD && tasks[i].id == 0u) {
            idx = i;
            break;
        }
    }
    if (idx == task_count) {
        if (task_count >= MAX_TASKS) return -1;
        task_count++;
        appended = 1u;
    }
    tasks[idx] = tasks[1];
    tasks[idx].id = scheduler_next_pid();
    tasks[idx].state = TASK_READY;
    tasks[idx].runtime_ticks = 0u;
    tasks[idx].vruntime = 0u;
    tasks[idx].has_context = 0;
    tasks[idx].address_space = PAGING_SPACE_KERNEL;
    tasks[idx].wait_child = 0u;
    tasks[idx].home_core = 0u;
    tasks[idx].migrations = 0u;
    tasks[idx].prio_original = 20u;
    tasks[idx].prio_inherited = 0u;
    tasks[idx].blocked_by = 0u;
    tasks[idx].sandbox_token = sandbox_open(tasks[idx].id % 16u, 4u * 1024u * 1024u, 200u, timer_ticks());
    if (tasks[idx].sandbox_token < 0) {
        tasks[idx].id = 0u;
        tasks[idx].state = TASK_DEAD;
        tasks[idx].has_context = 0;
        tasks[idx].address_space = PAGING_SPACE_KERNEL;
        if (appended) task_count--;
        return -1;
    }
    proc_core_register_task(tasks[idx].id, tasks[current].id);
    cgroup_bind_task(tasks[idx].id, 1u);
    namespace_bind_task(tasks[idx].id, 1u);
    security_set_task_caps(tasks[idx].id, 0u);
    scheduler_apply_user_syscall_policy(tasks[idx].id);
    *out_task_id = tasks[idx].id;
    return 0;
}



/* ── Thread creation (shares address space with parent) ───────────────── */
int scheduler_create_user_thread(u32 parent_task, u64 entry, u64 user_stack,
                                  const char* name, u32* out_task_id) {
    if (!out_task_id || task_count < 2u) return -1;
    /* entry/user_stack are caller-supplied: confine them to the user range the
     * same way scheduler_prepare_user_task does, so a thread can never start
     * executing on a kernel address. */
    if (entry < USER_VIRT_BASE || entry >= USER_VIRT_LIMIT ||
        user_stack < USER_VIRT_BASE || user_stack >= USER_VIRT_LIMIT) {
        return -1;
    }
    int pidx = task_index(parent_task);
    if (pidx <= 0) return -1;

    u32 idx = task_count;
    u32 appended = 0u;
    for (u32 i = 1u; i < task_count; i++) {
        if (tasks[i].state == TASK_DEAD && tasks[i].id == 0u) {
            idx = i;
            break;
        }
    }
    if (idx == task_count) {
        if (task_count >= MAX_TASKS) return -1;
        task_count++;
        appended = 1u;
    }
    tasks[idx] = tasks[1];
    tasks[idx].id = scheduler_next_pid();
    tasks[idx].state = TASK_READY;
    tasks[idx].runtime_ticks = 0u;
    tasks[idx].vruntime = 0u;
    tasks[idx].has_context = 0;
    sched_fpu_area_reset(tasks[idx].fxsave_area);
    tasks[idx].prio_original = tasks[pidx].priority;
    tasks[idx].prio_inherited = 0u;
    tasks[idx].blocked_by = 0u;
    /* Key difference from fork: share the parent's address space. */
    tasks[idx].address_space = tasks[pidx].address_space;
    tasks[idx].wait_child = 0u;
    tasks[idx].home_core = 0u;
    tasks[idx].migrations = 0u;
    tasks[idx].sandbox_token = sandbox_open(tasks[idx].id % 16u, 4u * 1024u * 1024u, 200u, timer_ticks());
    if (tasks[idx].sandbox_token < 0) {
        tasks[idx].id = 0u;
        tasks[idx].state = TASK_DEAD;
        tasks[idx].has_context = 0;
        tasks[idx].address_space = PAGING_SPACE_KERNEL;
        if (appended) task_count--;
        return -1;
    }
    /* Set up the user-space context. */
    tasks[idx].context.r15 = 0u;
    tasks[idx].context.r14 = 0u;
    tasks[idx].context.r13 = 0u;
    tasks[idx].context.r12 = 0u;
    tasks[idx].context.r11 = 0u;
    tasks[idx].context.r10 = 0u;
    tasks[idx].context.r9 = 0u;
    tasks[idx].context.r8 = 0u;
    tasks[idx].context.rdi = 0u;
    tasks[idx].context.rsi = 0u;
    tasks[idx].context.rdx = 0u;
    tasks[idx].context.rcx = 0u;
    tasks[idx].context.rbx = 0u;
    tasks[idx].context.rax = 0u;
    tasks[idx].context.int_no = 0u;
    tasks[idx].context.err_code = 0u;
    tasks[idx].context.rip = entry;
    tasks[idx].context.cs = 0x1Bu;
    tasks[idx].context.rflags = 0x202u;
    tasks[idx].context.rsp = user_stack;
    tasks[idx].context.ss = 0x23u;
    tasks[idx].has_context = 1;
    /* New thread gets a priority boost to start quickly. */
    tasks[idx].realtime = 1u;
    tasks[idx].priority = 1u;
    tasks[idx].policy = SCHED_POLICY_RT;

    proc_core_register_task(tasks[idx].id, parent_task);
    cgroup_bind_task(tasks[idx].id, 1u);
    namespace_bind_task(tasks[idx].id, 1u);
    security_set_task_caps(tasks[idx].id, 0u);
    scheduler_apply_user_syscall_policy(tasks[idx].id);
    *out_task_id = tasks[idx].id;
    return 0;
}

int scheduler_create_thread(u32 parent_task, u64 entry, u64 stack_top, u64 arg, u32* out_tid) {
    (void)arg;
    return scheduler_create_user_thread(parent_task, entry, stack_top, "thread", out_tid);
}

int scheduler_prepare_user_task(u32 task_id, u64 entry, u64 user_stack) {
    int idx = task_index(task_id);
    serial_write("[sched] prepare task=");
    serial_write_dec(task_id);
    serial_write(" idx=");
    serial_write_dec((u32)idx);
    serial_write(" entry=0x");
    serial_write_hex((u32)(entry >> 32));
    serial_write_hex((u32)(entry & 0xFFFFFFFFu));
    serial_write(" stk=0x");
    serial_write_hex((u32)(user_stack >> 32));
    serial_write_hex((u32)(user_stack & 0xFFFFFFFFu));
    serial_putc('\n');
    if (idx <= 0 || entry < USER_VIRT_BASE || entry >= USER_VIRT_LIMIT ||
        user_stack < USER_VIRT_BASE || user_stack >= USER_VIRT_LIMIT) {
        serial_write("[sched] prepare guard fail idx=");
        serial_write_dec((u32)idx);
        serial_write(" base=0x");
        serial_write_hex((u32)(USER_VIRT_BASE >> 12));
        serial_write(" lim=0x");
        serial_write_hex((u32)(USER_VIRT_LIMIT >> 12));
        serial_putc('\n');
        return -1;
    }
    tasks[idx].context.r15 = 0u;
    tasks[idx].context.r14 = 0u;
    tasks[idx].context.r13 = 0u;
    tasks[idx].context.r12 = 0u;
    tasks[idx].context.r11 = 0u;
    tasks[idx].context.r10 = 0u;
    tasks[idx].context.r9 = 0u;
    tasks[idx].context.r8 = 0u;
    tasks[idx].context.rdi = 0u;
    tasks[idx].context.rsi = 0u;
    tasks[idx].context.rdx = 0u;
    tasks[idx].context.rcx = 0u;
    tasks[idx].context.rbx = 0u;
    tasks[idx].context.rax = 0u;
    tasks[idx].context.int_no = 0u;
    tasks[idx].context.err_code = 0u;
    tasks[idx].context.rbp = 0u;
    tasks[idx].context.rip = entry;
    tasks[idx].context.cs = 0x1Bu;
    tasks[idx].context.rflags = 0x202u;
    tasks[idx].context.rsp = user_stack;
    tasks[idx].context.ss = 0x23u;
    tasks[idx].has_context = 1;
    tasks[idx].state = TASK_READY;
    sched_fpu_area_reset(tasks[idx].fxsave_area);
    memory_protect_set_task_stack(task_id, user_stack - (8u * 4096u), user_stack);
    /* A freshly loaded user image must get an initial scheduling turn even
       when the inherited service task has accumulated a larger vruntime. */
    tasks[idx].realtime = 1u;
    tasks[idx].priority = 1u;
    tasks[idx].policy = SCHED_POLICY_RT;
    return task_protect_validate_context(&tasks[idx].context) ? 0 : -1;
}

int scheduler_prepare_fork_task(u32 task_id, const registers_t* parent_regs, u32 address_space) {
    int idx = task_index(task_id);
    if (idx <= 0 || !parent_regs || address_space == PAGING_SPACE_KERNEL ||
        parent_regs->cs != 0x1Bu || parent_regs->ss != 0x23u) return -1;
    tasks[idx].context = *parent_regs;
    tasks[idx].context.rax = 0u;
    tasks[idx].context.int_no = 0u;
    tasks[idx].context.err_code = 0u;
    tasks[idx].address_space = address_space;
    tasks[idx].has_context = 1;
    tasks[idx].state = TASK_READY;
    tasks[idx].wait_child = 0u;
    tasks[idx].realtime = 1u;
    tasks[idx].priority = 1u;
    tasks[idx].policy = SCHED_POLICY_RT;
    sched_fpu_area_reset(tasks[idx].fxsave_area);
    memory_protect_set_task_stack(task_id, parent_regs->rsp - (8u * 4096u), parent_regs->rsp);
    return task_protect_validate_context(&tasks[idx].context) ? 0 : -1;
}

int scheduler_set_address_space(u32 task_id, u32 address_space) {
    int idx = task_index(task_id);
    if (idx <= 0 || address_space == PAGING_SPACE_KERNEL) return -1;
    tasks[idx].address_space = address_space;
    return 0;
}

u32 scheduler_task_address_space(u32 task_id) {
    int idx = task_index(task_id);
    return idx >= 0 ? tasks[idx].address_space : PAGING_SPACE_KERNEL;
}

u32 scheduler_context_switches(void) {
    return context_switch_count;
}

u32 scheduler_task_count(void) {
    return task_count;
}

int scheduler_set_priority(u32 task_id, u8 prio) {
    int idx = task_index(task_id);
    if (idx < 0 || idx == 0) {
        return -1;
    }
    if (prio > 39) {
        prio = 39;
    }
    tasks[idx].priority = prio;
    scheduler_sync_task_to_rust((u32)idx);
    return 0;
}

int scheduler_set_realtime(u32 task_id, int enabled) {
    int idx = task_index(task_id);
    if (idx < 0 || idx == 0) {
        return -1;
    }
    tasks[idx].realtime = enabled ? 1 : 0;
    {
        sched_queue_decision_t qd = task_queue_decision((u32)idx);
        tasks[idx].policy = (u8)derive_policy((u32)idx, &qd);
    }
    scheduler_sync_task_to_rust((u32)idx);
    return 0;
}

int scheduler_set_cgroup(u32 task_id, u32 cgroup_id) {
    int idx = task_index(task_id);
    if (idx < 0) {
        return -1;
    }
    if (cgroup_bind_task(task_id, cgroup_id) != 0) {
        return -1;
    }
    tasks[idx].cgroup_id = cgroup_id;
    return 0;
}

int scheduler_set_namespace(u32 task_id, u32 ns_id) {
    int idx = task_index(task_id);
    if (idx < 0) {
        return -1;
    }
    if (namespace_bind_task(task_id, ns_id) != 0) {
        return -1;
    }
    tasks[idx].namespace_id = ns_id;
    scheduler_sync_task_to_rust((u32)idx);
    return 0;
}

int scheduler_set_task_class(u32 task_id, sched_class_t task_class) {
    int idx = task_index(task_id);
    if (idx < 0 || idx == 0) {
        return -1;
    }
    if (task_class > SCHED_CLASS_NET) {
        task_class = SCHED_CLASS_CPU;
    }
    tasks[idx].task_class = (u8)task_class;
    {
        sched_queue_decision_t qd = task_queue_decision((u32)idx);
        tasks[idx].policy = (u8)derive_policy((u32)idx, &qd);
    }
    scheduler_sync_task_to_rust((u32)idx);
    return 0;
}

sched_class_t scheduler_task_class(u32 task_id) {
    int idx = task_index(task_id);
    if (idx < 0) {
        return SCHED_CLASS_CPU;
    }
    if (tasks[idx].task_class > SCHED_CLASS_NET) {
        return SCHED_CLASS_CPU;
    }
    return (sched_class_t)tasks[idx].task_class;
}

u32 scheduler_task_priority(u32 task_id) {
    int idx = task_index(task_id);
    if (idx < 0) {
        return 0;
    }
    return tasks[idx].priority;
}

u64 scheduler_task_vruntime(u32 task_id) {
    int idx = task_index(task_id);
    if (idx < 0) {
        return 0;
    }
    return tasks[idx].vruntime;
}

/* Exit performs address-space teardown while the interrupted user frame is
   still active; keep this transition outside compiler canary instrumentation
   just like the IRQ frame rewrite itself. */
__attribute__((no_stack_protector))
int scheduler_exit_task(u32 task_id, i32 exit_code) {
    int idx = task_index(task_id);
    int reap_after_cleanup = 0;
    u32 parent_task;
    if (idx <= 0) {
        return -1;
    }
    tasks[idx].state = TASK_DEAD;
    tasks[idx].realtime = 0;
    syscall_release_task(task_id);
    scheduler_sync_task_to_rust((u32)idx);
    parent_task = proc_core_parent(task_id);
    /* Kernel-launched processes have no user parent that can wait for them;
       keep no unreachable zombie after their resources are released. */
    if (parent_task == 0u) reap_after_cleanup = 1;
    i32 reaped_code = exit_code;
    (void)proc_core_mark_exit(task_id, exit_code);
    if (parent_task != 0u) {
        int parent_idx = task_index(parent_task);
        if (parent_idx > 0 && tasks[parent_idx].state == TASK_BLOCKED &&
            tasks[parent_idx].wait_child == task_id) {
            (void)proc_core_wait(task_id, &reaped_code);
            reap_after_cleanup = 1;
            tasks[parent_idx].wait_child = 0u;
            tasks[parent_idx].context.rax = (u64)(i32)reaped_code;
            tasks[parent_idx].state = TASK_READY;
        }
        /* ── Priority inheritance restoration ────────────────────────────
         * If the exiting child had its priority boosted via inheritance
         * (parent was waiting for it), restore the original priority. */
        if (tasks[idx].prio_inherited) {
            tasks[idx].priority = tasks[idx].prio_original;
            tasks[idx].prio_inherited = 0u;
        }
    }
    vm_destroy_task_regions(task_id);
    if (tasks[idx].address_space != PAGING_SPACE_KERNEL) {
        (void)elf32_destroy_user(task_id);
        tasks[idx].address_space = PAGING_SPACE_KERNEL;
    }
    /* Release the sandbox only after ELF pages have returned their charges. */
    if (tasks[idx].sandbox_token >= 0) {
        sandbox_close(tasks[idx].sandbox_token);
        tasks[idx].sandbox_token = -1;
    }
    if (reap_after_cleanup) (void)scheduler_reap_task(task_id);
    if (current == (u32)idx) {
        /* Keep the dying slot as current until scheduler_schedule_from_irq()
           saves the interrupted frame and selects the next runnable task. */
        (void)paging_space_switch(PAGING_SPACE_KERNEL);
        /* The interrupted frame is a ring-3 frame whose stack was torn down
           by the ELF cleanup above.  The iret from this interrupt must NOT
           return through idle's stale kernel frame (its rsp predates the
           user detour and the C stack below it is garbage); route it to a
           fresh continuation instead.  from_irq() sees the pending flag and
           rewrites the outgoing frame to the trampoline. */
        kernel_resume_pending = 1u;
    }
    return 0;
}

int scheduler_reap_task(u32 task_id) {
    int idx = task_index(task_id);
    if (idx <= 0 || tasks[idx].state != TASK_DEAD) return -1;
    /* The process core has already released the zombie record. Keep the
       scheduler slot reusable without compacting live task indices. */
    tasks[idx].id = 0u;
    tasks[idx].wait_child = 0u;
    tasks[idx].has_context = 0u;
    tasks[idx].address_space = PAGING_SPACE_KERNEL;
    tasks[idx].sandbox_token = -1;
    return 0;
}

int scheduler_kill_task(u32 task_id) {
    return scheduler_exit_task(task_id, 137);
}

int scheduler_current_user_frame(registers_t* out) {
    if (!out || current == 0u || current >= task_count || !tasks[current].has_context) return -1;
    *out = tasks[current].context;
    (void)paging_space_switch(tasks[current].address_space);
    return 0;
}

/* Restore a user register frame (sigreturn).
 *
 * The frame is read back from the *user* stack, where the process was free to
 * modify every field before invoking sigreturn. Copying it verbatim would let a
 * task pick its own segment selectors and EFLAGS and resume through iret with
 * kernel privileges (classic SROP). Only the general-purpose registers and a
 * range-checked eip/esp are taken from the frame; the privilege-relevant fields
 * are forced back to their user-mode values. */
int scheduler_restore_user_frame(u32 task_id, const registers_t* frame) {
    int idx = task_index(task_id);
    registers_t safe;
    if (idx <= 0 || !frame) return -1;
    if (frame->rip < USER_VIRT_BASE || frame->rip >= USER_VIRT_LIMIT) return -1;
    if (frame->rsp < USER_VIRT_BASE || frame->rsp >= USER_VIRT_LIMIT) return -1;

    safe = *frame;
    /* Non-negotiable: user code and data selectors only. */
    safe.cs = 0x1Bu;
    safe.ss = 0x23u;
    /* Keep only the arithmetic/direction flags the task may legitimately set;
     * always run with IF set and IOPL 0, and never inherit TF/NT/VM/AC. */
    safe.rflags = (frame->rflags & 0x0CD5u) | 0x202u;
    safe.int_no = 0u;
    safe.err_code = 0u;

    tasks[idx].context = safe;
    tasks[idx].has_context = 1;
    return 0;
}

int scheduler_task_is_user(u32 task_id) {
    int idx = task_index(task_id);
    if (idx <= 0 || tasks[idx].state == TASK_DEAD) return 0;
    return tasks[idx].address_space != PAGING_SPACE_KERNEL &&
           tasks[idx].has_context && tasks[idx].context.cs == 0x1Bu;
}

int scheduler_has_runnable_peer(u32 task_id) {
    for (u32 i = 1u; i < task_count; i++) {
        if (tasks[i].id == task_id || tasks[i].state != TASK_READY ||
            !tasks[i].has_context || tasks[i].address_space == PAGING_SPACE_KERNEL)
            continue;
        if (tasks[i].state != TASK_DEAD && cgroup_cpu_allow(tasks[i].id, timer_ticks()))
            return 1;
    }
    return 0;
}

int scheduler_task_sandbox(u32 task_id) {
    int idx = task_index(task_id);
    if (idx <= 0 || tasks[idx].sandbox_token < 0) {
        return -1;
    }
    return tasks[idx].sandbox_token;
}

int scheduler_block_wait_from_irq(registers_t* regs, u32 child_task) {
    if (!regs || child_task == 0u || current == 0u || current >= task_count) return -1;
    if (tasks[current].state == TASK_DEAD || task_index(child_task) < 0) return -1;

    /* ── Priority inheritance ──────────────────────────────────────────
     * Boost the child's priority to the parent's level so the child
     * gets scheduled sooner and releases the exit code faster. */
    {
        int cidx = task_index(child_task);
        if (cidx > 0 && tasks[cidx].priority > tasks[current].priority) {
            /* Only capture prio_original on the *first* boost. Re-capturing it
             * while already inherited would store the elevated value, so the
             * later restore would make the boost permanent. */
            if (!tasks[cidx].prio_inherited) {
                tasks[cidx].prio_original = tasks[cidx].priority;
            }
            tasks[cidx].priority = tasks[current].priority;
            tasks[cidx].prio_inherited = 1u;
            tasks[current].blocked_by = child_task;
        }
    }

    tasks[current].context = *regs;
    tasks[current].has_context = 1;
    tasks[current].wait_child = child_task;
    tasks[current].state = TASK_BLOCKED;
    current = 0u;
    scheduler_schedule_from_irq(regs);
    return 0;
}

int scheduler_block_task(u32 task_id) {
    int idx = task_index(task_id);
    if (idx <= 0 || tasks[idx].state == TASK_DEAD) return -1;

    /* ── Priority inheritance ──────────────────────────────────────────
     * When a high-priority task blocks (e.g. waiting for IPC from a
     * lower-priority holder), temporarily boost the holder's priority
     * to the blocked task's level.  This prevents priority inversion. */
    if (current != 0u && current != (u32)idx) {
        u32 holder = current;
        if (tasks[holder].priority > tasks[idx].priority) {
            /* First boost only — see scheduler_block_wait_from_irq. */
            if (!tasks[holder].prio_inherited) {
                tasks[holder].prio_original = tasks[holder].priority;
            }
            tasks[holder].priority = tasks[idx].priority;
            tasks[holder].prio_inherited = 1u;
            tasks[idx].blocked_by = tasks[holder].id;
        }
    }

    tasks[idx].state = TASK_BLOCKED;
    tasks[idx].wait_ticks = 0u;
    if (current == (u32)idx) {
        current = 0u;
        scheduler_schedule();
    }
    return 0;
}

int scheduler_wake_task(u32 task_id) {
    int idx = task_index(task_id);
    if (idx <= 0 || tasks[idx].state == TASK_DEAD) return -1;
    if (tasks[idx].state == TASK_BLOCKED) {
        tasks[idx].state = TASK_READY;
        tasks[idx].wait_ticks = 0u;
        tasks[idx].wake_predict = 100u;
        /* ── Priority inheritance restoration ────────────────────────
         * Restore the priority of whomever we were blocked on. */
        if (tasks[idx].blocked_by != 0u) {
            int bidx = task_index(tasks[idx].blocked_by);
            if (bidx > 0 && tasks[bidx].prio_inherited) {
                tasks[bidx].priority = tasks[bidx].prio_original;
                tasks[bidx].prio_inherited = 0u;
            }
            tasks[idx].blocked_by = 0u;
        }
        scheduler_sync_task_to_rust((u32)idx);
    }
    return 0;
}

u32 scheduler_task_state(u32 task_id) {
    int idx = task_index(task_id);
    if (idx < 0) return TASK_DEAD;
    return tasks[idx].state;
}

u32 scheduler_pick_oom_victim(const u8* protected_map, u32 map_len) {
    u32 victim = 0xFFFFFFFFu;
    u32 worst_prio = 0;
    u64 worst_vruntime = 0;

    for (u32 i = 1; i < task_count; i++) {
        if (tasks[i].state != TASK_READY) {
            continue;
        }
        if (i < map_len && protected_map && protected_map[tasks[i].id]) {
            continue;
        }

        if (victim == 0xFFFFFFFFu) {
            victim = tasks[i].id;
            worst_prio = tasks[i].priority;
            worst_vruntime = tasks[i].vruntime;
            continue;
        }

        if (tasks[i].priority > worst_prio ||
            (tasks[i].priority == worst_prio && tasks[i].vruntime > worst_vruntime)) {
            victim = tasks[i].id;
            worst_prio = tasks[i].priority;
            worst_vruntime = tasks[i].vruntime;
        }
    }
    return victim;
}

void scheduler_set_quantum_level(u32 level) {
    if (level < 1u) {
        level = 1u;
    } else if (level > 3u) {
        level = 3u;
    }
    quantum_level = level;
    quantum_ticks = level_to_ticks(level);
    if (slice_ticks > quantum_ticks) {
        slice_ticks = quantum_ticks;
    }
}

u32 scheduler_quantum_level(void) {
    return quantum_level;
}

u32 scheduler_quantum_ticks(void) {
    return quantum_ticks;
}

void scheduler_stats(scheduler_stats_t* out) {
    if (!out) {
        return;
    }
    out->task_count = task_count;
    out->current_task = scheduler_current_task();
    out->context_switches = context_switch_count;
    out->core_count = core_count;
    out->quantum_level = quantum_level;
    out->quantum_ticks = quantum_ticks;
    out->avg_slice_goal = dynamic_slice_goal;
    if (sample_ticks == 0u) {
        out->load_percent = 0u;
    } else {
        out->load_percent = (sample_busy * 100u) / sample_ticks;
    }
}

void scheduler_telemetry(scheduler_telemetry_t* out) {
    if (!out) {
        return;
    }
    scheduler_refresh_telemetry();
    *out = sched_tel;
}

int scheduler_task_telemetry(u32 task_id, scheduler_task_telemetry_t* out) {
    if (!out) {
        return -1;
    }
    int idx = task_index(task_id);
    if (idx < 0) {
        return -1;
    }
    out->task_id = tasks[idx].id;
    out->home_core = tasks[idx].home_core;
    out->cache_heat = tasks[idx].cache_heat;
    out->burst_score = tasks[idx].burst_score;
    out->pressure = tasks[idx].pressure;
    out->latency_budget = tasks[idx].latency_budget;
    out->wake_frequency = tasks[idx].wake_frequency;
    out->interaction_score = tasks[idx].interaction_score;
    out->queue_class = tasks[idx].queue_class;
    out->fast_queue = tasks[idx].fast_queue;
    out->policy = tasks[idx].policy;
    out->latency_score = clamp100(tasks[idx].wake_predict + tasks[idx].latency_target * 8u);
    out->wait_ticks = tasks[idx].wait_ticks;
    out->migrations = tasks[idx].migrations;
    return 0;
}

void scheduler_policy_stats(scheduler_policy_stats_t* out) {
    if (!out) {
        return;
    }
    refresh_policy_stats();
    *out = policy_stats;
}
