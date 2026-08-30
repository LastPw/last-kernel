#include "microvm.h"
#include "string.h"
#include "spinlock.h"

/* ==============================================================================
 * MicroVM Hypervisor & Container Isolation Subsystem
 * ============================================================================== */

static virtualization_status_t s_virt;
static micro_container_t s_containers[MAX_CONTAINERS];
static micro_vm_instance_t s_vms[MAX_MICROVMS];
static spinlock_t s_virt_lock;

void microvm_subsystem_init(void) {
    spinlock_init(&s_virt_lock);
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);

    s_virt.virt_type = VIRT_VMX; /* Intel VT-x with EPT hardware virtualization */
    s_virt.nested_virt_supported = 1;
    s_virt.ept_rvi_supported = 1;
    s_virt.active_containers = 0;
    s_virt.active_microvms = 0;

    for (u32 i = 0; i < MAX_CONTAINERS; i++) {
        s_containers[i].container_id = i;
        s_containers[i].active = 0;
        s_containers[i].root_pid = 0;
        s_containers[i].memory_limit_bytes = 0;
        s_containers[i].memory_used_bytes = 0;
        s_containers[i].cpu_quota_pct = 100;
        s_containers[i].cpu_shares = 1024;
        s_containers[i].max_file_descriptors = 1024;
        s_containers[i].open_fds = 0;
        s_containers[i].isolated_net = 1;
        s_containers[i].isolated_mount = 1;
    }

    for (u32 i = 0; i < MAX_MICROVMS; i++) {
        s_vms[i].vm_id = i;
        s_vms[i].active = 0;
        s_vms[i].vcpu_count = 0;
        s_vms[i].ram_size_bytes = 0;
        s_vms[i].guest_cr3 = 0;
        s_vms[i].guest_rip = 0;
        s_vms[i].exit_count = 0;
        s_vms[i].last_exit_reason = 0;
    }

    spinlock_unlock_irqrestore(&s_virt_lock, flags);
}

cpu_virt_type_t microvm_detect_hw_virt(void) {
    return s_virt.virt_type;
}

int container_create(u32 id, const char* name, u64 mem_limit, u32 cpu_quota) {
    if (id >= MAX_CONTAINERS) return -1;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);

    micro_container_t* c = &s_containers[id];
    c->container_id = id;
    c->active = 1;
    if (name) str_ncpy(c->name, name, sizeof(c->name));
    c->memory_limit_bytes = mem_limit;
    c->memory_used_bytes = 0;
    c->cpu_quota_pct = (cpu_quota == 0 || cpu_quota > 100) ? 100 : cpu_quota;
    c->cpu_shares = 1024;
    c->max_file_descriptors = 1024;
    c->open_fds = 0;
    c->isolated_net = 1;
    c->isolated_mount = 1;

    s_virt.active_containers++;
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

int container_destroy(u32 id) {
    if (id >= MAX_CONTAINERS) return -1;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);
    if (s_containers[id].active) {
        s_containers[id].active = 0;
        if (s_virt.active_containers > 0) s_virt.active_containers--;
    }
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

int container_attach_task(u32 id, u32 pid) {
    if (id >= MAX_CONTAINERS || !s_containers[id].active) return -1;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);
    s_containers[id].root_pid = pid;
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

int container_check_resource_limits(u32 id, u64 requested_bytes) {
    if (id >= MAX_CONTAINERS || !s_containers[id].active) return 0;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);
    if (s_containers[id].memory_limit_bytes > 0 &&
        (s_containers[id].memory_used_bytes + requested_bytes > s_containers[id].memory_limit_bytes)) {
        spinlock_unlock_irqrestore(&s_virt_lock, flags);
        return -1; /* OOM / Exceeded cgroups quota */
    }
    s_containers[id].memory_used_bytes += requested_bytes;
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

int microvm_create(u32 vm_id, const char* name, u32 vcpus, u64 ram_bytes) {
    if (vm_id >= MAX_MICROVMS) return -1;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);

    micro_vm_instance_t* vm = &s_vms[vm_id];
    vm->vm_id = vm_id;
    vm->active = 1;
    if (name) str_ncpy(vm->name, name, sizeof(vm->name));
    vm->vcpu_count = (vcpus == 0) ? 1 : vcpus;
    vm->ram_size_bytes = ram_bytes;
    vm->guest_cr3 = 0x100000;
    vm->guest_rip = 0xFFFFFFFF80000000ULL;
    vm->exit_count = 0;
    vm->last_exit_reason = 0;

    s_virt.active_microvms++;
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

int microvm_destroy(u32 vm_id) {
    if (vm_id >= MAX_MICROVMS) return -1;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);
    if (s_vms[vm_id].active) {
        s_vms[vm_id].active = 0;
        if (s_virt.active_microvms > 0) s_virt.active_microvms--;
    }
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

int microvm_run_step(u32 vm_id) {
    if (vm_id >= MAX_MICROVMS || !s_vms[vm_id].active) return -1;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);
    /* Emulate single VMLAUNCH / VMRESUME step & VM-Exit handling */
    s_vms[vm_id].exit_count++;
    s_vms[vm_id].last_exit_reason = 0x0A; /* CPUID or HLT exit */
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
    return 0;
}

void microvm_get_status(virtualization_status_t* out) {
    if (!out) return;
    u32 flags = spinlock_lock_irqsave(&s_virt_lock);
    *out = s_virt;
    spinlock_unlock_irqrestore(&s_virt_lock, flags);
}
