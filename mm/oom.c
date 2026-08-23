#include "oom.h"
#include "scheduler.h"
#include "memory_runtime.h"
#include "heap.h"

static u8 protected_task[32];
static u32 kill_count = 0;
static u32 last_killed = 0;

void oom_init(void) {
    for (u32 i = 0; i < 32; i++) {
        protected_task[i] = 0;
    }
    kill_count = 0;
    last_killed = 0;
    oom_mark_protected(0, 1);
}

void oom_mark_protected(u32 task_id, int protect) {
    if (task_id < 32) {
        protected_task[task_id] = protect ? 1 : 0;
    }
}

int oom_handle(u32 total_bytes, u32 used_bytes) {
    if (total_bytes == 0) {
        return 0;
    }

    u32 threshold = (total_bytes * 92u) / 100u;
    u32 pressure = memory_runtime_govern_pressure(total_bytes, used_bytes, heap_fragmentation_score());
    if (pressure >= 72u && used_bytes < threshold) {
        return 0;
    }
    if (used_bytes < threshold) {
        return 0;
    }

    u32 victim = scheduler_pick_oom_victim(protected_task, 32);
    if (victim == 0xFFFFFFFFu) {
        return -1;
    }
    if (scheduler_kill_task(victim) == 0) {
        kill_count++;
        last_killed = victim;
        return 1;
    }
    return -1;
}

u32 oom_kill_count(void) {
    return kill_count;
}

u32 oom_last_killed_task(void) {
    return last_killed;
}
