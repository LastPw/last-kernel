#ifndef MEMORY_PROTECT_H
#define MEMORY_PROTECT_H

#include "types.h"
#include "idt.h"

void memory_protect_init(void);
void memory_protect_guard_page(u64 addr);
int memory_protect_is_guard_fault(u64 fault_addr);
int memory_protect_validate_stack(u64 rsp);
void memory_protect_set_task_stack(u32 task_id, u64 low, u64 high);
int memory_protect_validate_task_stack(u32 task_id, u64 rsp);
int memory_protect_validate_user_buffer(u64 addr, u64 len, int write_access);
void memory_protect_scrub(void* ptr, u32 len);

#endif
