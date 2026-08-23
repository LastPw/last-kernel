#ifndef MEMORY_MONITOR_H
#define MEMORY_MONITOR_H

#include "types.h"

void memory_monitor_init(void);
void memory_monitor_on_alloc(void* ptr, u32 size, u32 task_id);
void memory_monitor_on_free(void* ptr, u32 task_id);
void memory_monitor_release_task(u32 task_id);
int memory_monitor_check_write(u32 task_id, u64 addr, u32 len);
u32 memory_monitor_task_bytes(u32 task_id);
u32 memory_monitor_total_bytes(void);

#endif
