#ifndef SANDBOX_H
#define SANDBOX_H

#include "types.h"

typedef struct {
    u8 active;
    u32 module_slot;
    u32 mem_limit_bytes;
    u32 tick_budget;
    u32 start_tick;
    u32 charged_bytes;
    u32 peak_bytes;
    u32 failed_allocations;
} sandbox_ctx_t;

typedef struct {
    u32 mem_limit_bytes;
    u32 cpu_limit_percent;
    u8 configured;
} sandbox_quota_t;

void sandbox_init(void);
int sandbox_open(u32 module_slot, u32 mem_limit_bytes, u32 tick_budget, u32 now_tick);
void sandbox_close(int token);
int sandbox_is_active(int token);
int sandbox_charge(int token, u32 bytes);
void sandbox_release(int token, u32 bytes);
int sandbox_get_usage(int token, u32* used, u32* peak, u32* failures);
int sandbox_set_module_quota(u32 module_slot, u32 mem_limit_bytes, u32 cpu_limit_percent);
int sandbox_get_module_quota(u32 module_slot, sandbox_quota_t* out);

#endif
