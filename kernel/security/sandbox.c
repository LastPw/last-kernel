#include "sandbox.h"

#define SANDBOX_SLOTS 8
#define SANDBOX_MODULES 16

static sandbox_ctx_t ctx[SANDBOX_SLOTS];
static sandbox_quota_t quotas[SANDBOX_MODULES];

void sandbox_init(void) {
    for (u32 i = 0; i < SANDBOX_SLOTS; i++) {
        ctx[i].active = 0;
        ctx[i].module_slot = 0;
        ctx[i].mem_limit_bytes = 0;
        ctx[i].tick_budget = 0;
        ctx[i].start_tick = 0;
        ctx[i].charged_bytes = 0;
        ctx[i].peak_bytes = 0;
        ctx[i].failed_allocations = 0;
    }
    for (u32 i = 0; i < SANDBOX_MODULES; i++) {
        quotas[i].configured = 0;
        quotas[i].mem_limit_bytes = 0;
        quotas[i].cpu_limit_percent = 0;
    }
}

int sandbox_open(u32 module_slot, u32 mem_limit_bytes, u32 tick_budget, u32 now_tick) {
    if (module_slot < SANDBOX_MODULES && quotas[module_slot].configured) {
        if (mem_limit_bytes == 0) {
            mem_limit_bytes = quotas[module_slot].mem_limit_bytes;
        }
        if (tick_budget == 0) {
            u32 cpu = quotas[module_slot].cpu_limit_percent;
            if (cpu == 0) {
                cpu = 20;
            }
            tick_budget = cpu * 2u;
        }
    }

    for (u32 i = 0; i < SANDBOX_SLOTS; i++) {
        if (!ctx[i].active) {
            ctx[i].active = 1;
            ctx[i].module_slot = module_slot;
            ctx[i].mem_limit_bytes = mem_limit_bytes;
            ctx[i].tick_budget = tick_budget;
            ctx[i].start_tick = now_tick;
            ctx[i].charged_bytes = 0;
            ctx[i].peak_bytes = 0;
            ctx[i].failed_allocations = 0;
            return (int)i;
        }
    }
    return -1;
}

void sandbox_close(int token) {
    if (token < 0 || token >= SANDBOX_SLOTS) {
        return;
    }
    ctx[token].active = 0;
}

int sandbox_is_active(int token) {
    if (token < 0 || token >= SANDBOX_SLOTS) {
        return 0;
    }
    return ctx[token].active ? 1 : 0;
}

int sandbox_charge(int token, u32 bytes) {
    if (token < 0 || token >= SANDBOX_SLOTS || !ctx[token].active || bytes == 0u) {
        return -1;
    }
    if (ctx[token].mem_limit_bytes != 0u &&
        (bytes > ctx[token].mem_limit_bytes ||
         ctx[token].charged_bytes > ctx[token].mem_limit_bytes - bytes)) {
        ctx[token].failed_allocations++;
        return -1;
    }
    if (ctx[token].charged_bytes > 0xFFFFFFFFu - bytes) {
        ctx[token].failed_allocations++;
        return -1;
    }
    ctx[token].charged_bytes += bytes;
    if (ctx[token].charged_bytes > ctx[token].peak_bytes) {
        ctx[token].peak_bytes = ctx[token].charged_bytes;
    }
    return 0;
}

void sandbox_release(int token, u32 bytes) {
    if (token < 0 || token >= SANDBOX_SLOTS || !ctx[token].active || bytes == 0u) {
        return;
    }
    if (bytes >= ctx[token].charged_bytes) {
        ctx[token].charged_bytes = 0u;
    } else {
        ctx[token].charged_bytes -= bytes;
    }
}

int sandbox_get_usage(int token, u32* used, u32* peak, u32* failures) {
    if (token < 0 || token >= SANDBOX_SLOTS || !ctx[token].active) {
        return -1;
    }
    if (used) *used = ctx[token].charged_bytes;
    if (peak) *peak = ctx[token].peak_bytes;
    if (failures) *failures = ctx[token].failed_allocations;
    return 0;
}

int sandbox_set_module_quota(u32 module_slot, u32 mem_limit_bytes, u32 cpu_limit_percent) {
    if (module_slot >= SANDBOX_MODULES) {
        return -1;
    }
    if (cpu_limit_percent > 100) {
        cpu_limit_percent = 100;
    }
    quotas[module_slot].configured = 1;
    quotas[module_slot].mem_limit_bytes = mem_limit_bytes;
    quotas[module_slot].cpu_limit_percent = cpu_limit_percent;
    return 0;
}

int sandbox_get_module_quota(u32 module_slot, sandbox_quota_t* out) {
    if (module_slot >= SANDBOX_MODULES || !out) {
        return -1;
    }
    *out = quotas[module_slot];
    return 0;
}
