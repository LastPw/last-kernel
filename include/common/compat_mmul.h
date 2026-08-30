#ifndef COMPAT_MMUL_H
#define COMPAT_MMUL_H

#include "compat.h"
#include "resource_monitor.h"
#include "types.h"

typedef struct {
    u8 valid;
    u32 object_count;
    u32 total_size_kb;
    u32 total_commit_kb;
    u32 fault_count;
} mmul_profile_t;

void compat_mmul_init(void);
void compat_mmul_bind_task(u32 task_id, compat_personality_t personality);
void compat_mmul_fault(u32 task_id, u8 write_fault, u8 exec_fault);
u32 compat_mmul_semantic_cost(compat_personality_t personality, u32 code);
u32 compat_mmul_exception_cost(compat_personality_t personality, u32 code);
void compat_mmul_adapt(u32 task_id, compat_personality_t personality, const resource_snapshot_t* rs, u32* mem_kb, u32* threads, u32* io_score);
int compat_mmul_profile(u32 task_id, mmul_profile_t* out);

#endif

