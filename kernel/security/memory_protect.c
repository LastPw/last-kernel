#include "memory_protect.h"
#include "paging.h"
#include "security.h"

#define MAX_GUARD_PAGES 16
#define MAX_STACK_TRACK 64

typedef struct {
    u64 addr;
    u8 active;
} guard_page_t;

typedef struct {
    u8 active;
    u32 task_id;
    u64 low;
    u64 high;
} stack_range_t;

static guard_page_t guards[MAX_GUARD_PAGES];
static stack_range_t stacks[MAX_STACK_TRACK];

void memory_protect_init(void) {
    for (u32 i = 0; i < MAX_GUARD_PAGES; i++) {
        guards[i].addr = 0;
        guards[i].active = 0;
    }
    for (u32 i = 0; i < MAX_STACK_TRACK; i++) {
        stacks[i].active = 0;
        stacks[i].task_id = 0u;
        stacks[i].low = 0u;
        stacks[i].high = 0u;
    }
}

void memory_protect_guard_page(u64 addr) {
    const security_profile_t* sp = security_profile();
    if (!sp->guard_pages_enabled) {
        return;
    }

    u64 page_addr = addr & 0xFFFFFFFFFFFFF000ull;
    paging_set_page_flags(page_addr, 0);

    for (u32 i = 0; i < MAX_GUARD_PAGES; i++) {
        if (!guards[i].active) {
            guards[i].active = 1;
            guards[i].addr = page_addr;
            return;
        }
    }
}

int memory_protect_is_guard_fault(u64 fault_addr) {
    u64 page_addr = fault_addr & 0xFFFFFFFFFFFFF000ull;
    for (u32 i = 0; i < MAX_GUARD_PAGES; i++) {
        if (guards[i].active && guards[i].addr == page_addr) {
            return 1;
        }
    }
    return 0;
}

int memory_protect_validate_stack(u64 rsp) {
    if (rsp < 0x20000 || rsp > 0x90000) {
        return 0;
    }
    return 1;
}

void memory_protect_set_task_stack(u32 task_id, u64 low, u64 high) {
    if (high <= low) {
        return;
    }

    for (u32 i = 0; i < MAX_STACK_TRACK; i++) {
        if (stacks[i].active && stacks[i].task_id == task_id) {
            stacks[i].low = low;
            stacks[i].high = high;
            return;
        }
    }

    for (u32 i = 0; i < MAX_STACK_TRACK; i++) {
        if (!stacks[i].active) {
            stacks[i].active = 1u;
            stacks[i].task_id = task_id;
            stacks[i].low = low;
            stacks[i].high = high;
            return;
        }
    }
}

int memory_protect_validate_task_stack(u32 task_id, u64 rsp) {
    for (u32 i = 0; i < MAX_STACK_TRACK; i++) {
        if (!stacks[i].active || stacks[i].task_id != task_id) {
            continue;
        }
        if (rsp >= stacks[i].low && rsp <= stacks[i].high) {
            return 1;
        }
        return 0;
    }
    return memory_protect_validate_stack(rsp);
}

int memory_protect_validate_user_buffer(u64 addr, u64 len, int write_access) {
    const security_profile_t* sp = security_profile();
    if (!sp->isolation_enabled) {
        return 1;
    }
    return paging_validate_user_ptr(addr, len, write_access);
}

void memory_protect_scrub(void* ptr, u32 len) {
    if (!ptr || len == 0) {
        return;
    }

    volatile u8* p = (volatile u8*)ptr;
    for (u32 i = 0; i < len; i++) {
        p[i] = 0;
    }
}
