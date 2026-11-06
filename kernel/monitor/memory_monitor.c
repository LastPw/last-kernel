#include "memory_monitor.h"
#include "task_protect.h"
#include "memory_protect.h"
#include "threat_logger.h"
#include "cgroup.h"
#include "advanced_allocator.h"

#define MEM_TASK_MAX 32
#define MEM_ALLOC_MAX 512

typedef struct {
    u32 task_id;
    u32 bytes;
    u8 used;
} mem_task_t;

static mem_task_t mt[MEM_TASK_MAX];
typedef struct {
    void* ptr;
    u32 size;
    u32 task_id;
    u8 used;
} mem_alloc_t;

static mem_alloc_t allocations[MEM_ALLOC_MAX];
static u32 total_bytes = 0;

static int task_slot(u32 task_id) {
    for (u32 i = 0; i < MEM_TASK_MAX; i++) {
        if (mt[i].used && mt[i].task_id == task_id) {
            return (int)i;
        }
    }
    return -1;
}

void memory_monitor_init(void) {
    total_bytes = 0;
    for (u32 i = 0; i < MEM_TASK_MAX; i++) {
        mt[i].used = 0;
        mt[i].task_id = 0;
        mt[i].bytes = 0;
    }
    for (u32 i = 0; i < MEM_ALLOC_MAX; i++) {
        allocations[i].used = 0u;
        allocations[i].ptr = 0;
        allocations[i].size = 0u;
        allocations[i].task_id = 0u;
    }
}

void memory_monitor_on_alloc(void* ptr, u32 size, u32 task_id) {
    if (!ptr || size == 0) {
        return;
    }

    u32 projected = memory_monitor_task_bytes(task_id) + size;
    if (!cgroup_mem_allow(task_id, projected)) {
        threat_logger_log(THREAT_HEAP_CORRUPT, task_id, projected, 0);
        return;
    }

    int s = task_slot(task_id);
    if (s < 0) {
        for (u32 i = 0; i < MEM_TASK_MAX; i++) {
            if (!mt[i].used) {
                mt[i].used = 1;
                mt[i].task_id = task_id;
                mt[i].bytes = size;
                s = (int)i;
                break;
            }
        }
        if (s < 0) return;
    }
    mt[s].bytes += size;
    total_bytes += size;

    for (u32 i = 0; i < MEM_ALLOC_MAX; i++) {
        if (!allocations[i].used) {
            allocations[i].used = 1u;
            allocations[i].ptr = ptr;
            allocations[i].size = size;
            allocations[i].task_id = task_id;
            return;
        }
    }
}

void memory_monitor_on_free(void* ptr, u32 task_id) {
    (void)task_id;
    if (!ptr) return;
    for (u32 i = 0; i < MEM_ALLOC_MAX; i++) {
        if (!allocations[i].used || allocations[i].ptr != ptr) continue;
        u32 owner = allocations[i].task_id;
        u32 size = allocations[i].size;
        int s = task_slot(owner);
        if (s >= 0) {
            mt[s].bytes = mt[s].bytes > size ? mt[s].bytes - size : 0u;
            if (mt[s].bytes == 0u) mt[s].used = 0u;
        }
        total_bytes = total_bytes > size ? total_bytes - size : 0u;
        allocations[i].used = 0u;
        allocations[i].ptr = 0;
        allocations[i].size = 0u;
        allocations[i].task_id = 0u;
        return;
    }
}

void memory_monitor_release_task(u32 task_id) {
    for (u32 i = 0; i < MEM_ALLOC_MAX; i++) {
        if (!allocations[i].used || allocations[i].task_id != task_id) continue;
        void* ptr = allocations[i].ptr;
        allocations[i].used = 0u;
        allocations[i].ptr = 0;
        allocations[i].size = 0u;
        allocations[i].task_id = 0u;
        adv_free(ptr);
    }
    int s = task_slot(task_id);
    if (s >= 0) {
        total_bytes = total_bytes > mt[s].bytes ? total_bytes - mt[s].bytes : 0u;
        mt[s].used = 0u;
        mt[s].bytes = 0u;
        mt[s].task_id = 0u;
    }
}

int memory_monitor_check_write(u32 task_id, u64 addr, u32 len) {
    if (!task_protect_can_access(task_id, addr, len)) {
        threat_logger_log(THREAT_KERNEL_WRITE, task_id, addr, len);
        return 0;
    }

    if (!memory_protect_validate_stack(addr)) {
        threat_logger_log(THREAT_STACK_CORRUPT, task_id, addr, len);
        return 0;
    }

    return 1;
}

u32 memory_monitor_task_bytes(u32 task_id) {
    int s = task_slot(task_id);
    if (s < 0) {
        return 0;
    }
    return mt[s].bytes;
}

u32 memory_monitor_total_bytes(void) {
    return total_bytes;
}
