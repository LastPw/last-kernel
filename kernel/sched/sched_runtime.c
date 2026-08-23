#include "sched_runtime.h"

static sched_runtime_snapshot_t g_snap;
static u32 g_core_count = 1u;
static u32 g_idle_cores = 0u;

static u32 clamp100_local(u32 v) {
    return v > 100u ? 100u : v;
}

void sched_runtime_init(void) {
    g_core_count = 1u;
    g_idle_cores = 0u;
    g_snap.hot_depth = 0u;
    g_snap.warm_depth = 0u;
    g_snap.cold_depth = 0u;
    g_snap.render_depth = 0u;
    g_snap.cursor_depth = 0u;
    g_snap.audio_depth = 0u;
    g_snap.input_depth = 0u;
    g_snap.latency_spikes = 0u;
    g_snap.core_pressure = 0u;
    g_snap.idle_ratio = 0u;
    g_snap.fast_path_hits = 0u;
    g_snap.migration_limited = 0u;
}

void sched_runtime_begin_sample(u32 core_count, u32 idle_cores) {
    g_core_count = core_count ? core_count : 1u;
    g_idle_cores = idle_cores;
    g_snap.hot_depth = 0u;
    g_snap.warm_depth = 0u;
    g_snap.cold_depth = 0u;
    g_snap.render_depth = 0u;
    g_snap.cursor_depth = 0u;
    g_snap.audio_depth = 0u;
    g_snap.input_depth = 0u;
    g_snap.latency_spikes = 0u;
    g_snap.fast_path_hits = 0u;
    g_snap.migration_limited = 0u;
    g_snap.idle_ratio = clamp100_local((g_idle_cores * 100u) / g_core_count);
}

sched_queue_decision_t sched_runtime_classify(const sched_task_model_t* task) {
    sched_queue_decision_t d;
    u32 urgency = 0u;

    d.temp_queue = SCHED_TEMP_COLD;
    d.fast_queue = SCHED_FAST_NONE;
    d.preempt_boost = 0u;
    d.wake_cost = 10u;
    d.lock_cost = 10u;
    d.migration_limit = 0u;

    if (!task) {
        return d;
    }

    urgency = task->interaction_score;
    urgency += task->wake_frequency / 2u;
    urgency += task->cache_heat / 4u;
    if (task->latency_budget <= 2u) urgency += 25u;
    if (task->realtime) urgency += 30u;
    urgency = clamp100_local(urgency);

    if (urgency >= 72u || task->latency_budget <= 1u) {
        d.temp_queue = SCHED_TEMP_HOT;
    } else if (urgency >= 38u || task->cache_heat >= 35u) {
        d.temp_queue = SCHED_TEMP_WARM;
    }

    if (task->realtime) {
        d.fast_queue = SCHED_FAST_AUDIO;
    } else if (task->latency_budget <= 1u && task->interaction_score >= 70u) {
        d.fast_queue = SCHED_FAST_CURSOR;
    } else if (task->interaction_score >= 55u) {
        d.fast_queue = SCHED_FAST_INPUT;
    } else if (task->task_class == 0u && task->cache_heat >= 45u) {
        d.fast_queue = SCHED_FAST_RENDER;
    }

    if (d.fast_queue != SCHED_FAST_NONE) {
        d.preempt_boost = 24u + urgency / 4u;
        d.wake_cost = 2u;
        d.lock_cost = 2u;
        d.migration_limit = 1u;
    } else if (d.temp_queue == SCHED_TEMP_HOT) {
        d.preempt_boost = 12u;
        d.wake_cost = 4u;
        d.lock_cost = 5u;
        d.migration_limit = task->cache_heat > 60u ? 1u : 0u;
    } else if (d.temp_queue == SCHED_TEMP_WARM) {
        d.preempt_boost = 5u;
        d.wake_cost = 7u;
        d.lock_cost = 8u;
    }

    if (task->thermal_class >= 2u && task->pressure > 75u) {
        if (d.preempt_boost > 8u) d.preempt_boost -= 8u;
        d.migration_limit = 0u;
    }

    return d;
}

void sched_runtime_note_task(const sched_queue_decision_t* decision, u32 wait_ticks, u32 pressure) {
    if (!decision) return;

    if (decision->temp_queue == SCHED_TEMP_HOT) g_snap.hot_depth++;
    else if (decision->temp_queue == SCHED_TEMP_WARM) g_snap.warm_depth++;
    else g_snap.cold_depth++;

    if (decision->fast_queue == SCHED_FAST_RENDER) g_snap.render_depth++;
    else if (decision->fast_queue == SCHED_FAST_CURSOR) g_snap.cursor_depth++;
    else if (decision->fast_queue == SCHED_FAST_AUDIO) g_snap.audio_depth++;
    else if (decision->fast_queue == SCHED_FAST_INPUT) g_snap.input_depth++;

    if (decision->fast_queue != SCHED_FAST_NONE) g_snap.fast_path_hits++;
    if (decision->migration_limit) g_snap.migration_limited++;
    if (wait_ticks > 24u || pressure > 85u) g_snap.latency_spikes++;
}

void sched_runtime_finish_sample(u32 core_pressure) {
    g_snap.core_pressure = clamp100_local(core_pressure);
}

void sched_runtime_snapshot(sched_runtime_snapshot_t* out) {
    if (out) *out = g_snap;
}
