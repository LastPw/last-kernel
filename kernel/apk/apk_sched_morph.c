#include "apk_morph.h"
#include "scheduler.h"
#include "string.h"
#include "serial.h"
#include "spinlock.h"

/*
 * APK Quantum-Adaptive Multi-Policy Scheduler Morpher
 *
 * Dynamically modulates kernel scheduling algorithms, quantum slices,
 * fastlane latency boosts, and power throttling according to real-time workload.
 */

static apk_morph_state_t s_morph_state;
static spinlock_t s_morph_lock;
static u8 s_morph_inited = 0;

static const char* s_mode_labels[5] = {
    "BALANCED",
    "HIGH_THROUGHPUT",
    "ESPORTS_LOW_LATENCY",
    "HARDENED_SECURITY",
    "GREEN_ECO_SAVER"
};

static void apply_profile(u32 mode) {
    apk_sched_profile_t* p = &s_morph_state.current_profile;

    switch (mode) {
        case APK_MORPH_THROUGHPUT:
            p->target_quantum_ticks = 20; /* 20ms slices */
            p->fastlane_boost_pct = 10;
            p->cfs_latency_target_ms = 40;
            p->io_burst_credit = 100;
            p->power_cstate_limit = 0;
            p->strict_security_barriers = 0;
            scheduler_set_quantum_level(3);
            break;

        case APK_MORPH_LOW_LATENCY:
            p->target_quantum_ticks = 1;  /* 1ms slices (1000Hz) */
            p->fastlane_boost_pct = 100;  /* Max fastlane priority */
            p->cfs_latency_target_ms = 6;
            p->io_burst_credit = 50;
            p->power_cstate_limit = 0;
            p->strict_security_barriers = 0;
            scheduler_set_quantum_level(1);
            break;

        case APK_MORPH_HARDENED:
            p->target_quantum_ticks = 4;
            p->fastlane_boost_pct = 20;
            p->cfs_latency_target_ms = 16;
            p->io_burst_credit = 20;
            p->power_cstate_limit = 1;
            p->strict_security_barriers = 1;
            scheduler_set_quantum_level(2);
            break;

        case APK_MORPH_GREEN:
            p->target_quantum_ticks = 10;
            p->fastlane_boost_pct = 0;
            p->cfs_latency_target_ms = 30;
            p->io_burst_credit = 10;
            p->power_cstate_limit = 3;    /* Maximum C-State deep sleep */
            p->strict_security_barriers = 0;
            scheduler_set_quantum_level(4);
            break;

        default: /* BALANCED */
            p->target_quantum_ticks = 4;  /* 4ms slices */
            p->fastlane_boost_pct = 40;
            p->cfs_latency_target_ms = 20;
            p->io_burst_credit = 40;
            p->power_cstate_limit = 1;
            p->strict_security_barriers = 0;
            scheduler_set_quantum_level(2);
            break;
    }
}

int apk_morph_init(void) {
    if (s_morph_inited) return 0;
    spinlock_init(&s_morph_lock);

    memset(&s_morph_state, 0, sizeof(s_morph_state));
    s_morph_state.active_mode = APK_MORPH_BALANCED;
    apply_profile(APK_MORPH_BALANCED);

    s_morph_inited = 1;
    serial_write("[APK_MORPH] Quantum-Adaptive Multi-Policy Scheduler Morpher Active\n");
    return 0;
}

int apk_morph_set_mode(u32 mode) {
    if (mode > APK_MORPH_GREEN) return -1;
    if (!s_morph_inited) apk_morph_init();

    spinlock_lock(&s_morph_lock);
    if (s_morph_state.active_mode == mode) {
        spinlock_unlock(&s_morph_lock);
        return 0;
    }

    s_morph_state.active_mode = mode;
    s_morph_state.total_switches++;
    apply_profile(mode);
    spinlock_unlock(&s_morph_lock);

    serial_write("[APK_MORPH] Switched Scheduler Profile to: ");
    serial_write(s_mode_labels[mode]);
    serial_write("\n");
    return 0;
}

u32 apk_morph_get_mode(void) {
    return s_morph_state.active_mode;
}

const char* apk_morph_get_mode_name(u32 mode) {
    if (mode > APK_MORPH_GREEN) return "UNKNOWN";
    return s_mode_labels[mode];
}

void apk_morph_feed_telemetry(u32 input_events, u32 io_ops, u32 cpu_busy_pct) {
    if (!s_morph_inited) return;

    spinlock_lock(&s_morph_lock);
    s_morph_state.input_burst_score = input_events;
    s_morph_state.io_throughput_score = io_ops;
    s_morph_state.cpu_compute_score = cpu_busy_pct;

    u32 current = s_morph_state.active_mode;
    u32 target = current;

    if (input_events > 40) {
        target = APK_MORPH_LOW_LATENCY;
    } else if (io_ops > 100 || cpu_busy_pct > 85) {
        target = APK_MORPH_THROUGHPUT;
    } else if (cpu_busy_pct < 5 && input_events == 0) {
        target = APK_MORPH_GREEN;
    } else if (current != APK_MORPH_BALANCED) {
        target = APK_MORPH_BALANCED;
    }

    if (target != current) {
        s_morph_state.active_mode = target;
        s_morph_state.total_switches++;
        apply_profile(target);
    }
    spinlock_unlock(&s_morph_lock);
}

void apk_morph_get_state(apk_morph_state_t* out) {
    if (!out) return;
    if (!s_morph_inited) apk_morph_init();
    spinlock_lock(&s_morph_lock);
    *out = s_morph_state;
    spinlock_unlock(&s_morph_lock);
}
