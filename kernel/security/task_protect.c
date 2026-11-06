#include "task_protect.h"

#define TASK_MAX_PROTECT 16

typedef struct {
    u8 active;
    u64 base;
    u64 limit;
    task_privilege_t priv;
} task_sec_t;

static task_sec_t table[TASK_MAX_PROTECT];

void task_protect_init(void) {
    for (u32 i = 0; i < TASK_MAX_PROTECT; i++) {
        table[i].active = 0;
        table[i].base = 0;
        table[i].limit = 0;
        table[i].priv = TASK_PRIV_KERNEL;
    }
}

void task_protect_set_range(u32 task_id, u64 base, u64 limit, task_privilege_t priv) {
    if (task_id >= TASK_MAX_PROTECT) {
        return;
    }

    table[task_id].active = 1;
    table[task_id].base = base;
    table[task_id].limit = limit;
    table[task_id].priv = priv;
}

int task_protect_can_access(u32 task_id, u64 addr, u32 len) {
    if (task_id >= TASK_MAX_PROTECT || !table[task_id].active) {
        return 0;
    }

    u64 end = addr + len;
    if (end < addr) {
        return 0;
    }

    return addr >= table[task_id].base && end <= table[task_id].limit;
}

int task_protect_validate_context(const registers_t* regs) {
    if (!regs) {
        return 0;
    }

    if (regs->cs == 0x08) {
        /* Kernel continuation can live in the identity-mapped kernel window or higher-half */
        return (regs->rip >= 0x200000ull && regs->rip < 0x20000000ull) || (regs->rip >= KERNEL_VIRT_BASE);
    }
    if (regs->cs == 0x1Bu) {
        if (regs->ss != 0x23u) return 0;
        if (regs->rip < USER_VIRT_BASE || regs->rip >= USER_VIRT_LIMIT) return 0;
        if (regs->rsp < USER_VIRT_BASE || regs->rsp >= USER_VIRT_LIMIT) return 0;
        return 1;
    }
    return 0;
}
