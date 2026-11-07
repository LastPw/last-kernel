/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: timer.c
 * Purpose: High-precision system timer, PIT/LAPIC management, and safe delay loops
 * =============================================================================
 */

#include "timer.h"
#include "io.h"
#include "scheduler.h"
#include "nic.h"
#include "netstack.h"
#include "self_heal.h"
#include "sdc.h"
#include "ai_security.h"
#include "hal.h"
#include "ultimate.h"
#include "ultimate_kernel.h"
#include "pcoe2.h"
#include "vm.h"
#include "watchdog.h"
#include "kshield.h"
#include "apic.h"
#include "interrupts.h"
#include "serial.h"

static u32 ticks = 0;
static u32 frequency = 100;
static u8 runtime_ready = 0;
static u32 runtime_features = TIMER_FEAT_SCHED | TIMER_FEAT_MONITOR;
static u32 runtime_target_features = TIMER_FEAT_SCHED | TIMER_FEAT_MONITOR;
static u8 in_tick = 0;
static u8 use_lapic_timer = 0u;

/* LAPIC timer calibration reference */
static u32 lapic_timer_init_count = 0u;

static int feature_on(u32 flag) {
    return (runtime_features & flag) != 0u;
}

void timer_set_frequency(u32 frequency_hz) {
    if (frequency_hz < 20) {
        frequency_hz = 20;
    }
    if (frequency_hz > 1000) {
        frequency_hz = 1000;
    }

    if (use_lapic_timer && apic_is_available()) {
        /* Recalculate LAPIC timer initial count */
        u32 freq_hz_apic = apic_timer_freq_khz() * 1000u;
        if (freq_hz_apic > 0u) {
            lapic_timer_init_count = freq_hz_apic / frequency_hz;
            if (lapic_timer_init_count == 0u) lapic_timer_init_count = 1u;
            apic_lapic_write(LAPIC_TIMER_INIT, lapic_timer_init_count);
        }
        frequency = frequency_hz;
        return;
    }

    u32 divisor = 1193180 / frequency_hz;

    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));
    frequency = frequency_hz;
}

void timer_init(u32 frequency_hz) {
    ticks = 0;
    runtime_ready = 0;
    runtime_features = TIMER_FEAT_SCHED | TIMER_FEAT_MONITOR;
    runtime_target_features = TIMER_FEAT_SCHED | TIMER_FEAT_MONITOR;
    in_tick = 0;
    use_lapic_timer = 0u;
    lapic_timer_init_count = 0u;

    /* Try to use the LAPIC timer if APIC is available */
    if (apic_is_available()) {
        u8 timer_vector = 0x20u;  /* Use IRQ 0 vector */
        if (apic_timer_init(frequency_hz, timer_vector) == 0) {
            use_lapic_timer = 1u;
            lapic_timer_init_count = (apic_timer_freq_khz() * 1000u) / frequency_hz;
            serial_write("[TIMER] using LAPIC timer @ ");
            serial_write_dec(frequency_hz);
            serial_writeln(" Hz");
            return;
        }
        serial_writeln("[TIMER] LAPIC timer init failed, falling back to PIT");
    }

    /* Fall back to PIT */
    timer_set_frequency(frequency_hz);
}

static void timer_runtime_promote_features(void) {
    u32 next = runtime_features;

    if (runtime_features == runtime_target_features) {
        return;
    }

    next &= runtime_target_features;
    /* Late bring-up clears every runtime feature while the boot stack is in
       use.  Restore the baseline too: the old code only promoted optional
       features, so SCHED and MONITOR stayed off forever and no timer interrupt
       could perform the requested initial ring-3 switch. */
    next |= runtime_target_features & (TIMER_FEAT_SCHED | TIMER_FEAT_MONITOR);

    if ((runtime_target_features & TIMER_FEAT_NET) && ticks >= 200u) {
        next |= TIMER_FEAT_NET;
    }
    if ((runtime_target_features & TIMER_FEAT_RESOURCE) && ticks >= 400u) {
        next |= TIMER_FEAT_RESOURCE;
    }
    if ((runtime_target_features & TIMER_FEAT_VM_COMPAT) && ticks >= 700u) {
        next |= TIMER_FEAT_VM_COMPAT;
    }
    if ((runtime_target_features & TIMER_FEAT_SECURITY) && ticks >= 1000u) {
        next |= TIMER_FEAT_SECURITY;
    }
    if ((runtime_target_features & TIMER_FEAT_ENGINE) && ticks >= 1300u) {
        next |= TIMER_FEAT_ENGINE;
    }
    if ((runtime_target_features & TIMER_FEAT_SCANNERS) && ticks >= 1600u) {
        next |= TIMER_FEAT_SCANNERS;
    }

    runtime_features = next & TIMER_FEAT_ALL;
}

void timer_on_irq(registers_t* regs) {
    ticks++;

    if (!runtime_ready) {
        return;
    }
    if (in_tick) {
        return;
    }
    in_tick = 1;
    timer_runtime_promote_features();

    if (feature_on(TIMER_FEAT_SCHED)) {
        scheduler_tick(regs);
    }
    if (feature_on(TIMER_FEAT_MONITOR) && (ticks % 5u) == 0u) {
    }
    if (feature_on(TIMER_FEAT_NET) && (ticks % 4u) == 0u) {
        nic_poll(8);
        netstack_tick();
    }
    if (feature_on(TIMER_FEAT_RESOURCE) && (ticks % 10u) == 0u) {
        watchdog_tick();
    }
    if (feature_on(TIMER_FEAT_SECURITY) && (ticks % 20u) == 0u) {
        self_heal_tick();
        sdc_tick();
        ai_security_tick();
        kshield_tick();
    }
    if (feature_on(TIMER_FEAT_VM_COMPAT) && (ticks % 16u) == 0u) {
        vm_reclaim_tick();
    }
    if (feature_on(TIMER_FEAT_ENGINE) && (ticks % 20u) == 0u) {
        hal_tick();
        ultimate_tick();
        pcoe2_tick();
        ukernel_tick();
    }
    if (feature_on(TIMER_FEAT_SCANNERS) && (ticks % 50u) == 0u) {
        lastmap_tick();
        
    }

    in_tick = 0;
}

u8 timer_using_lapic(void) {
    return use_lapic_timer;
}

u32 timer_lapic_init_count(void) {
    return lapic_timer_init_count;
}

u32 timer_ticks(void) {
    return ticks;
}

u32 timer_frequency(void) {
    return frequency;
}

void timer_runtime_set_ready(u8 ready) {
    runtime_ready = ready ? 1u : 0u;
}

u8 timer_runtime_ready(void) {
    return runtime_ready;
}

void timer_runtime_set_features(u32 feature_mask) {
    runtime_features = feature_mask & TIMER_FEAT_ALL;
    if ((runtime_target_features & runtime_features) != runtime_features) {
        runtime_target_features = runtime_features;
    }
}

u32 timer_runtime_features(void) {
    return runtime_features;
}

void timer_runtime_set_target_features(u32 feature_mask) {
    runtime_target_features = feature_mask & TIMER_FEAT_ALL;
    runtime_features &= runtime_target_features;
}

u32 timer_runtime_target_features(void) {
    return runtime_target_features;
}

__attribute__((weak)) void lastmap_tick(void) {}
__attribute__((weak)) void lastshark_tick(void) {}

void timer_delay_ms(u32 ms) {
    u64 flags;
    __asm__ __volatile__("pushfq; popq %0" : "=r"(flags));
    if ((flags & 0x200ull) == 0) {
        /* Interrupts are disabled; ticks will never advance. Use CPU pause loop */
        for (volatile u32 i = 0; i < ms * 40000u; i++) {
            asm volatile("pause");
        }
        return;
    }
    u32 start = ticks;
    u32 wait_ticks = (ms * frequency + 999u) / 1000u;
    if (wait_ticks == 0) wait_ticks = 1;
    while ((ticks - start) < wait_ticks) {
        asm volatile("pause");
    }
}
