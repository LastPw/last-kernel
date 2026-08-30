#ifndef MULTICORE_SCHEDULER_H
#define MULTICORE_SCHEDULER_H

#include "types.h"

#define MCS_MAX_CORES 64u

void mcs_init(u32 core_count);
u32 mcs_core_count(void);
u32 mcs_pick_core(void);
void mcs_account_tick(u32 core, int busy);
void mcs_snapshot(u32* busy_ticks, u32* total_ticks, u32 max_cores);

/* Per-Core Runqueue & Work-Stealing */
int mcs_enqueue_task(u32 core, u32 task_id);
u32 mcs_dequeue_task(u32 core);
u32 mcs_work_steal(u32 core);

#endif
