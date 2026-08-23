#ifndef CGROUP_H
#define CGROUP_H

#include "types.h"

#define CGROUP_MAX 8
#define CGROUP_NAME_MAX 16

typedef struct {
    u32 id;
    char name[CGROUP_NAME_MAX];
    u32 cpu_limit_percent;
    u32 mem_limit_kb;
    u32 io_limit_ops;
    u32 cpu_window_ticks;
    u32 cpu_used_ticks;
    u8 active;
} cgroup_info_t;

void cgroup_init(void);
int cgroup_create(const char* name);
int cgroup_set_limits(u32 id, u32 cpu_percent, u32 mem_kb, u32 io_ops);
int cgroup_bind_task(u32 task_id, u32 cgroup_id);
u32 cgroup_task_group(u32 task_id);
int cgroup_cpu_allow(u32 task_id, u32 now_tick);
int cgroup_mem_allow(u32 task_id, u32 new_total_bytes);
u32 cgroup_count(void);
int cgroup_get_by_index(u32 idx, cgroup_info_t* out);

#endif
