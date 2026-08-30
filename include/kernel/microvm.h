#ifndef MICROVM_H
#define MICROVM_H

#include "types.h"

#define MAX_CONTAINERS          32
#define MAX_MICROVMS            16

typedef enum {
    VIRT_NONE   = 0,
    VIRT_VMX    = 1,   /* Intel VT-x */
    VIRT_SVM    = 2    /* AMD-V */
} cpu_virt_type_t;

typedef struct {
    u32 container_id;
    u8  active;
    char name[32];
    u32 root_pid;
    u64 memory_limit_bytes;
    u64 memory_used_bytes;
    u32 cpu_quota_pct;
    u32 cpu_shares;
    u32 max_file_descriptors;
    u32 open_fds;
    u8  isolated_net;
    u8  isolated_mount;
} micro_container_t;

typedef struct {
    u32 vm_id;
    u8  active;
    char name[32];
    u32 vcpu_count;
    u64 ram_size_bytes;
    u64 guest_cr3;
    u64 guest_rip;
    u64 exit_count;
    u32 last_exit_reason;
} micro_vm_instance_t;

typedef struct {
    cpu_virt_type_t virt_type;
    u8              nested_virt_supported;
    u8              ept_rvi_supported;
    u32             active_containers;
    u32             active_microvms;
} virtualization_status_t;

/* Virtualization & Container API */
void microvm_subsystem_init(void);
cpu_virt_type_t microvm_detect_hw_virt(void);

int container_create(u32 id, const char* name, u64 mem_limit, u32 cpu_quota);
int container_destroy(u32 id);
int container_attach_task(u32 id, u32 pid);
int container_check_resource_limits(u32 id, u64 requested_bytes);

int microvm_create(u32 vm_id, const char* name, u32 vcpus, u64 ram_bytes);
int microvm_destroy(u32 vm_id);
int microvm_run_step(u32 vm_id);

void microvm_get_status(virtualization_status_t* out);

#endif /* MICROVM_H */
