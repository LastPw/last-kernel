#ifndef RESOURCE_ENGINE_H
#define RESOURCE_ENGINE_H

#include "types.h"
#include "power.h"

#define RE_TASK_MAX 32

typedef enum {
    RE_PROFILE_AUTO = 0,
    RE_PROFILE_DESKTOP_LIGHT = 1,
    RE_PROFILE_DESKTOP_FULL = 2,
    RE_PROFILE_SERVER_LIGHT = 3,
    RE_PROFILE_SERVER_FULL = 4
} re_profile_t;

typedef enum {
    RE_TASK_CPU = 0,
    RE_TASK_IO = 1,
    RE_TASK_NET = 2
} re_task_class_t;

typedef struct {
    u32 thermal_celsius;
    u32 overcommit_permille;
    u32 low_mem_warning;
    u32 predicted_next_alloc;
    u32 recommended_threads;
    power_mode_t recommended_mode;
    re_profile_t profile;
} resource_engine_snapshot_t;

void resource_engine_init(u32 total_ram_bytes, u32 cores, u8 modern_profile);
void resource_engine_set_profile(re_profile_t profile);
re_profile_t resource_engine_get_profile(void);
u32 resource_engine_predict_alloc(u32 task_id, u32 request_bytes, u32 used_bytes, u32 total_bytes, u32 sandbox_limit_bytes);
void resource_engine_note_alloc(u32 task_id, u32 size);
void resource_engine_note_free(u32 task_id, u32 size);
void resource_engine_note_io(u32 task_id, u32 bytes, int is_net);
void resource_engine_tick(u32 cpu_busy_percent, u32 mem_used_bytes, u32 mem_total_bytes, u32 cores);
power_mode_t resource_engine_recommend_power_mode(void);
u32 resource_engine_recommend_threads(u32 workload_score, u32 cores, u8 realtime);
void resource_engine_snapshot(resource_engine_snapshot_t* out);

#endif
