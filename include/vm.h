#ifndef VM_H
#define VM_H

#include "types.h"

#define VM_PROT_READ  0x01u
#define VM_PROT_WRITE 0x02u
#define VM_PROT_EXEC  0x04u

#define VM_FLAG_GUARD_LOW  0x01u
#define VM_FLAG_GUARD_HIGH 0x02u
#define VM_FLAG_COW        0x04u

typedef enum {
    VM_POLICY_ANON = 0,
    VM_POLICY_HEAP = 1,
    VM_POLICY_STACK = 2,
    VM_POLICY_FILE = 3
} vm_policy_t;

typedef enum {
    VM_FAULT_UNHANDLED = 0,
    VM_FAULT_HANDLED = 1,
    VM_FAULT_PROTECTION = 2,
    VM_FAULT_GUARD = 3,
    VM_FAULT_OOM = 4
} vm_fault_result_t;

typedef struct {
    u8 used;
    u32 id;
    u32 task_id;
    u32 start;
    u32 size;
    u32 prot;
    vm_policy_t policy;
    u32 flags;
    u32 page_count;
    u32 committed_pages;
    u32 max_commit_pages;
    u32 fault_count;
    u32 guard_hits;
} vm_region_info_t;

typedef struct {
    u32 total_regions;
    u32 active_regions;
    u32 total_faults;
    u32 handled_faults;
    u32 protection_faults;
    u32 guard_faults;
    u32 oom_faults;
    u32 committed_pages;
    u32 cow_maps;
    u32 cow_splits;
} vm_stats_t;

void vm_init(u32 total_ram_bytes);
int vm_create_region(u32 task_id, u32 start, u32 size, u32 prot, vm_policy_t policy, u32 flags);
int vm_destroy_region(u32 task_id, u32 region_id);
void vm_destroy_task_regions(u32 task_id);
int vm_register_task_space(u32 task_id, u32 start, u32 end);
vm_fault_result_t vm_handle_page_fault(u32 task_id, u32 fault_addr, u32 err_code, u32 eip);
int vm_region_info(u32 task_id, u32 region_id, vm_region_info_t* out);
void vm_stats(vm_stats_t* out);
void vm_reclaim_tick(void);
int vm_set_region_flags(u32 task_id, u32 region_id, u32 flags_set, u32 flags_clear);

#endif
