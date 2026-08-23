#ifndef HYPER_GHOST_H
#define HYPER_GHOST_H

#include "types.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "hal.h"

typedef struct {
    u32 score;
    u32 pressure;
    u32 latency_shadow;
    u32 reserved_kb;
    u32 hot_pool_kb;
    u32 warm_pool_kb;
    u32 cold_pool_kb;
    u32 shadow_pages;
    u32 compression_permille;
    u32 dedup_permille;
    u32 reclaim_events;
    u32 rebalance_events;
    u32 low_mem_events;
    u32 policy_mode;
} hyper_ghost_status_t;

void hyper_ghost_init(u32 total_ram_bytes);
void hyper_ghost_tick(const resource_snapshot_t* rs,
                      const resource_engine_snapshot_t* re,
                      const hal_status_t* hs,
                      u32 mode);
u32 hyper_ghost_score(void);
u32 hyper_ghost_pressure(void);
u32 hyper_ghost_latency_shadow(void);
u32 hyper_ghost_reserved_kb(void);
void hyper_ghost_status(hyper_ghost_status_t* out);
int  hyper_ghost_freeze_task(u32 task_id);
int  hyper_ghost_thaw_task(u32 task_id);
u32  hyper_ghost_frozen_task_count(void);

#endif
