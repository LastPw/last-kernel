#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "types.h"

typedef struct {
    u32 ticks;
    u32 stalls_detected;
    u32 heap_faults;
    u32 vm_oom_spikes;
    u32 last_switch_tick;
    u32 healthy;
    u32 mitigation_actions;
    u32 process_recoveries;
} watchdog_status_t;

typedef struct {
    u32 stall_threshold_ticks;
    u32 vm_oom_spike_threshold;
    u32 heap_check_period;
    u32 auto_mitigate;
} watchdog_policy_t;

void watchdog_init(void);
void watchdog_tick(void);
void watchdog_status(watchdog_status_t* out);
void watchdog_set_policy(const watchdog_policy_t* policy);
void watchdog_get_policy(watchdog_policy_t* out);

#endif
