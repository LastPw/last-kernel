#include "compat_mmul.h"

#define MMUL_MAX_OBJ 256u

typedef struct {
    u8 used;
    u32 owner_task;
    u32 semantics;
    u32 size_kb;
    u32 commit_kb;
    u32 faults;
} mmul_obj_t;

static mmul_obj_t g_obj[MMUL_MAX_OBJ];

static mmul_obj_t* alloc_obj(u32 task_id, u32 semantics, u32 size_kb) {
    for (u32 i = 0; i < MMUL_MAX_OBJ; i++) {
        if (!g_obj[i].used) {
            g_obj[i].used = 1;
            g_obj[i].owner_task = task_id;
            g_obj[i].semantics = semantics;
            g_obj[i].size_kb = size_kb;
            g_obj[i].commit_kb = size_kb / 4u;
            g_obj[i].faults = 0;
            return &g_obj[i];
        }
    }
    return 0;
}

void compat_mmul_init(void) {
    for (u32 i = 0; i < MMUL_MAX_OBJ; i++) g_obj[i].used = 0;
}

void compat_mmul_bind_task(u32 task_id, compat_personality_t personality) {
    u32 semantics = (personality == COMPAT_PERS_WINDOWS) ? 2u :
        ((personality == COMPAT_PERS_LINUX) ? 3u : 1u);
    (void)alloc_obj(task_id, semantics, 1024u);
}

void compat_mmul_fault(u32 task_id, u8 write_fault, u8 exec_fault) {
    for (u32 i = 0; i < MMUL_MAX_OBJ; i++) {
        if (!g_obj[i].used || g_obj[i].owner_task != task_id) continue;
        g_obj[i].faults++;
        if (write_fault && g_obj[i].commit_kb < g_obj[i].size_kb) g_obj[i].commit_kb++;
        if (exec_fault) g_obj[i].semantics = 2u;
        return;
    }
}

u32 compat_mmul_semantic_cost(compat_personality_t personality, u32 code) {
    u32 idx = code & 511u;
    if (personality == COMPAT_PERS_WINDOWS) return (idx % 13u) + 2u;
    if (personality == COMPAT_PERS_LINUX) return (idx % 9u) + 1u;
    return (idx % 7u) + 1u;
}

u32 compat_mmul_exception_cost(compat_personality_t personality, u32 code) {
    u32 idx = code & 511u;
    if (personality == COMPAT_PERS_WINDOWS) return (idx % 29u) + 2u;
    if (personality == COMPAT_PERS_LINUX) return (idx % 17u) + 1u;
    return (idx % 11u) + 1u;
}

void compat_mmul_adapt(u32 task_id, compat_personality_t personality, const resource_snapshot_t* rs, u32* mem_kb, u32* threads, u32* io_score) {
    if (!rs || !mem_kb || !threads || !io_score) return;
    u32 sem_cost = compat_mmul_semantic_cost(personality, rs->predicted_next_alloc);
    u32 ex_cost = compat_mmul_exception_cost(personality, rs->net_packets);

    u32 m = *mem_kb + (sem_cost * 8u) + (ex_cost * 4u);
    if (rs->low_mem_warning) {
        if (m > 256u) m -= 128u;
    }
    if (m < 128u) m = 128u;
    if (m > 32768u) m = 32768u;
    *mem_kb = m;

    u32 t = *threads + (sem_cost / 5u);
    if (rs->cpu_busy_percent > 80u && t > 1u) t--;
    if (t < 1u) t = 1u;
    if (t > 16u) t = 16u;
    *threads = t;

    u32 io = *io_score + (rs->io_queue_depth / 2u) + (rs->net_packets / 32u);
    if (io > 100u) io = 100u;
    *io_score = io;

    u32 sig = rs->cpu_busy_percent + rs->io_queue_depth + rs->net_packets;
    if ((sig & 3u) == 0u) {
        compat_mmul_fault(task_id, (u8)(sig & 1u), (u8)((sig >> 1u) & 1u));
    }
}

int compat_mmul_profile(u32 task_id, mmul_profile_t* out) {
    if (!out) return -1;
    out->valid = 1;
    out->object_count = 0;
    out->total_size_kb = 0;
    out->total_commit_kb = 0;
    out->fault_count = 0;
    for (u32 i = 0; i < MMUL_MAX_OBJ; i++) {
        if (!g_obj[i].used || g_obj[i].owner_task != task_id) continue;
        out->object_count++;
        out->total_size_kb += g_obj[i].size_kb;
        out->total_commit_kb += g_obj[i].commit_kb;
        out->fault_count += g_obj[i].faults;
    }
    if (out->object_count == 0u) {
        out->valid = 0;
        return -1;
    }
    return 0;
}
