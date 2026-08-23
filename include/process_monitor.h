#ifndef PROCESS_MONITOR_H
#define PROCESS_MONITOR_H

#include "types.h"

#define PROCESS_MON_MAX 16
#define PROCESS_NAME_MAX 32

typedef struct {
    u32 task_id;
    char name[PROCESS_NAME_MAX];
    u32 runtime_ticks;
    u32 suspicious_score;
    u8 blocked;
} process_info_t;

void process_monitor_init(void);
void process_monitor_register(u32 task_id);
void process_monitor_register_named(u32 task_id, const char* name);
void process_monitor_unregister(u32 task_id);
void process_monitor_on_tick(u32 task_id, int busy);
int process_monitor_is_blocked(u32 task_id);
int process_monitor_snapshot(u32 task_id, process_info_t* out);
int process_monitor_snapshot_at(u32 index, process_info_t* out);
u32 process_monitor_task_count(void);
u32 process_monitor_blocked_count(void);

#endif
