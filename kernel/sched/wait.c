#include "wait.h"
#include "scheduler.h"

void wait_queue_init(wait_queue_t* q) {
    if (!q) return;
    q->count = 0u;
    for (u32 i = 0u; i < WAIT_QUEUE_MAX; i++) q->waiters[i] = 0u;
}

int wait_queue_add_current(wait_queue_t* q) {
    if (!q || q->count >= WAIT_QUEUE_MAX) return -1;
    u32 task = scheduler_current_task();
    if (task == 0u) return -1;
    for (u32 i = 0u; i < q->count; i++) {
        if (q->waiters[i] == task) return 0;
    }
    q->waiters[q->count++] = task;
    return scheduler_block_task(task);
}

int wait_queue_remove(wait_queue_t* q, u32 task_id) {
    if (!q || task_id == 0u) return -1;
    for (u32 i = 0u; i < q->count; i++) {
        if (q->waiters[i] != task_id) continue;
        q->waiters[i] = q->waiters[q->count - 1u];
        q->waiters[q->count - 1u] = 0u;
        q->count--;
        return 0;
    }
    return -1;
}

int wait_queue_wake_one(wait_queue_t* q) {
    if (!q || q->count == 0u) return -1;
    u32 task = q->waiters[0];
    (void)wait_queue_remove(q, task);
    return scheduler_wake_task(task);
}

u32 wait_queue_wake_all(wait_queue_t* q) {
    if (!q) return 0u;
    u32 woken = 0u;
    while (q->count != 0u) {
        if (wait_queue_wake_one(q) == 0) woken++;
    }
    return woken;
}

u32 wait_queue_count(const wait_queue_t* q) {
    return q ? q->count : 0u;
}
