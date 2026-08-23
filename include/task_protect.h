#ifndef TASK_PROTECT_H
#define TASK_PROTECT_H

#include "types.h"
#include "idt.h"
#include "paging.h"

typedef enum {
    TASK_PRIV_KERNEL = 0,
    TASK_PRIV_USER = 1
} task_privilege_t;

void task_protect_init(void);
void task_protect_set_range(u32 task_id, u64 base, u64 limit, task_privilege_t priv);
int task_protect_can_access(u32 task_id, u64 addr, u32 len);
int task_protect_validate_context(const registers_t* regs);

#endif
