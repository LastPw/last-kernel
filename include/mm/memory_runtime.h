#ifndef MEMORY_RUNTIME_H
#define MEMORY_RUNTIME_H

#include "types.h"

#define MEMRT_VAULT_MAX 16u
#define MEMRT_ZC_MAX 16u

typedef enum {
    MEMRT_TEMP_COLD = 0,
    MEMRT_TEMP_WARM = 1,
    MEMRT_TEMP_HOT = 2
} memrt_temp_t;

typedef enum {
    MEMRT_TAG_GENERAL = 0,
    MEMRT_TAG_RENDER = 1,
    MEMRT_TAG_TASK = 2,
    MEMRT_TAG_CACHE = 3,
    MEMRT_TAG_SECRET = 4,
    MEMRT_TAG_NETWORK = 5,
    MEMRT_TAG_STORAGE = 6,
    MEMRT_TAG_GPU = 7
} memrt_tag_t;

typedef enum {
    MEMRT_ZC_GPU = 0,
    MEMRT_ZC_NETWORK = 1,
    MEMRT_ZC_STORAGE = 2
} memrt_zero_copy_kind_t;

typedef struct {
    u32 hot_bytes;
    u32 warm_bytes;
    u32 cold_bytes;
    u32 vault_bytes;
    u32 vault_slots;
    u32 zero_copy_arenas;
    u32 predicted_alloc;
    u32 pressure_score;
    u32 reclaim_events;
    u32 compression_events;
    u32 migration_events;
    u32 anti_rowhammer_gaps;
    u32 fragmentation_score;
    u32 numa_node;
    u32 locality_score;
} memrt_snapshot_t;

void memory_runtime_init(u32 total_bytes);
u32 memory_runtime_predict(u32 task_id, u32 request_bytes, memrt_tag_t tag);
void memory_runtime_on_alloc(void* ptr, u32 size, u32 task_id, memrt_tag_t tag);
void memory_runtime_on_free(void* ptr, u32 task_id);
u32 memory_runtime_govern_pressure(u32 total_bytes, u32 used_bytes, u32 fragmentation);
int memory_runtime_vault_store(const void* data, u32 len, u32* out_handle);
int memory_runtime_vault_load(u32 handle, void* out, u32 out_len);
int memory_runtime_vault_zeroize(u32 handle);
int memory_runtime_zero_copy_create(memrt_zero_copy_kind_t kind, void* ptr, u32 len, u32* out_id);
int memory_runtime_zero_copy_get(u32 id, memrt_zero_copy_kind_t* kind, void** ptr, u32* len);
void memory_runtime_snapshot(memrt_snapshot_t* out);

#endif
