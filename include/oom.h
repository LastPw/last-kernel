#ifndef OOM_H
#define OOM_H

#include "types.h"

void oom_init(void);
void oom_mark_protected(u32 task_id, int protect);
int oom_handle(u32 total_bytes, u32 used_bytes);
u32 oom_kill_count(void);
u32 oom_last_killed_task(void);

#endif
