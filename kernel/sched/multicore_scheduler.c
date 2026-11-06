#include "multicore_scheduler.h"

static u32 g_core_count = 1;
static u32 g_pick_cursor = 0;
static u32 g_busy_ticks[MCS_MAX_CORES] __attribute__((aligned(64)));
static u32 g_total_ticks[MCS_MAX_CORES] __attribute__((aligned(64)));

#include "spinlock.h"

#define MCS_MAX_TASKS_PER_CORE 64u

typedef struct {
    spinlock_t lock;
    u32 count;
    u32 head;
    u32 tail;
    u32 tasks[MCS_MAX_TASKS_PER_CORE];
} mcs_core_rq_t;

static mcs_core_rq_t g_core_rq[MCS_MAX_CORES];

void mcs_init(u32 core_count) {
    if (core_count == 0) {
        core_count = 1;
    }
    if (core_count > MCS_MAX_CORES) {
        core_count = MCS_MAX_CORES;
    }

    g_core_count = core_count;
    g_pick_cursor = 0;

    for (u32 i = 0; i < MCS_MAX_CORES; i++) {
        g_busy_ticks[i] = 0;
        g_total_ticks[i] = 0;
        spinlock_init(&g_core_rq[i].lock);
        g_core_rq[i].count = 0;
        g_core_rq[i].head = 0;
        g_core_rq[i].tail = 0;
    }
}

u32 mcs_core_count(void) {
    return g_core_count;
}

u32 mcs_pick_core(void) {
    if (g_core_count <= 1) {
        return 0;
    }

    /* Find least loaded core */
    u32 best_core = 0;
    u32 min_tasks = 0xFFFFFFFFu;
    for (u32 i = 0; i < g_core_count; i++) {
        if (g_core_rq[i].count < min_tasks) {
            min_tasks = g_core_rq[i].count;
            best_core = i;
        }
    }
    return best_core;
}

int mcs_enqueue_task(u32 core, u32 task_id) {
    if (core >= g_core_count) core = 0;
    mcs_core_rq_t* rq = &g_core_rq[core];

    u32 flags = spinlock_lock_irqsave(&rq->lock);
    if (rq->count >= MCS_MAX_TASKS_PER_CORE) {
        spinlock_unlock_irqrestore(&rq->lock, flags);
        return -1;
    }

    rq->tasks[rq->tail] = task_id;
    rq->tail = (rq->tail + 1u) % MCS_MAX_TASKS_PER_CORE;
    rq->count++;
    spinlock_unlock_irqrestore(&rq->lock, flags);
    return 0;
}

u32 mcs_dequeue_task(u32 core) {
    if (core >= g_core_count) core = 0;
    mcs_core_rq_t* rq = &g_core_rq[core];

    u32 flags = spinlock_lock_irqsave(&rq->lock);
    if (rq->count == 0) {
        spinlock_unlock_irqrestore(&rq->lock, flags);
        /* Try Work-Stealing from other cores */
        return mcs_work_steal(core);
    }

    u32 task_id = rq->tasks[rq->head];
    rq->head = (rq->head + 1u) % MCS_MAX_TASKS_PER_CORE;
    rq->count--;
    spinlock_unlock_irqrestore(&rq->lock, flags);
    return task_id;
}

u32 mcs_work_steal(u32 core) {
    if (g_core_count <= 1) return 0;

    for (u32 i = 0; i < g_core_count; i++) {
        u32 target = (core + 1u + i) % g_core_count;
        if (target == core) continue;

        mcs_core_rq_t* victim = &g_core_rq[target];
        if (victim->count > 1) {
            u32 flags = spinlock_lock_irqsave(&victim->lock);
            if (victim->count > 1) {
                /* Steal from tail */
                victim->tail = (victim->tail + MCS_MAX_TASKS_PER_CORE - 1u) % MCS_MAX_TASKS_PER_CORE;
                u32 stolen_task = victim->tasks[victim->tail];
                victim->count--;
                spinlock_unlock_irqrestore(&victim->lock, flags);
                return stolen_task;
            }
            spinlock_unlock_irqrestore(&victim->lock, flags);
        }
    }
    return 0;
}

void mcs_account_tick(u32 core, int busy) {
    if (core >= g_core_count) {
        core = 0;
    }

    g_total_ticks[core]++;
    if (busy) {
        g_busy_ticks[core]++;
    }
}

void mcs_snapshot(u32* busy_ticks, u32* total_ticks, u32 max_cores) {
    if (!busy_ticks || !total_ticks) {
        return;
    }

    for (u32 i = 0; i < max_cores; i++) {
        if (i < g_core_count) {
            busy_ticks[i] = g_busy_ticks[i];
            total_ticks[i] = g_total_ticks[i];
        } else {
            busy_ticks[i] = 0;
            total_ticks[i] = 0;
        }
    }
}

