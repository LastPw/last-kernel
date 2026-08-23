#ifndef TIMER_H
#define TIMER_H

#include "types.h"
#include "idt.h"

#define TIMER_FEAT_SCHED      (1u << 0)
#define TIMER_FEAT_MONITOR    (1u << 1)
#define TIMER_FEAT_NET        (1u << 2)
#define TIMER_FEAT_RESOURCE   (1u << 3)
#define TIMER_FEAT_SECURITY   (1u << 4)
#define TIMER_FEAT_VM_COMPAT  (1u << 5)
#define TIMER_FEAT_ENGINE     (1u << 6)
#define TIMER_FEAT_SCANNERS   (1u << 7)
#define TIMER_FEAT_ALL        0xFFu

void timer_init(u32 frequency_hz);
void timer_set_frequency(u32 frequency_hz);
u32 timer_frequency(void);
void timer_on_irq(registers_t* regs);
u32 timer_ticks(void);
void timer_runtime_set_ready(u8 ready);
u8 timer_runtime_ready(void);
void timer_runtime_set_features(u32 feature_mask);
u32 timer_runtime_features(void);
void timer_runtime_set_target_features(u32 feature_mask);
u32 timer_runtime_target_features(void);
u8 timer_using_lapic(void);
u32 timer_lapic_init_count(void);
void timer_delay_ms(u32 ms);

#endif
