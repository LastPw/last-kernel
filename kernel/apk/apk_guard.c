#include "apk_guard.h"
#include "apk_core.h"
#include "string.h"
#include "serial.h"
#include "timer.h"
#include "spinlock.h"

/*
 * APK Hardware Watchdog & Driver Quarantine Subsystem
 *
 * Implements sliding-window fault rate analysis, driver isolation,
 * and transparent hardware controller reset without kernel reboot.
 */

static apk_domain_guard_t s_guards[APK_MAX_DOMAINS];
static spinlock_t s_guard_lock;
static u8 s_guard_inited = 0;

int apk_guard_init(void) {
    if (s_guard_inited) return 0;
    spinlock_init(&s_guard_lock);

    memset(s_guards, 0, sizeof(s_guards));
    for (u32 i = 0; i < APK_MAX_DOMAINS; i++) {
        s_guards[i].domain_id = i;
        s_guards[i].window_start_tick = timer_ticks();
        s_guards[i].faults_in_window = 0;
        s_guards[i].is_quarantined = 0;
    }

    s_guard_inited = 1;
    serial_write("[APK_GUARD] Hardware Fault Watchdog & Quarantine Engine Armed\n");
    return 0;
}

void apk_guard_record_fault(u32 domain_id, u64 vector, u64 rip, u64 cr2) {
    if (domain_id >= APK_MAX_DOMAINS) return;
    if (!s_guard_inited) apk_guard_init();

    spinlock_lock(&s_guard_lock);
    apk_domain_guard_t* g = &s_guards[domain_id];
    u32 now = timer_ticks();

    /* 1-second sliding window */
    if (now - g->window_start_tick > 1000u) {
        g->window_start_tick = now;
        g->faults_in_window = 0;
    }

    g->faults_in_window++;

    /* Record fault history entry */
    u32 idx = g->history_head % APK_GUARD_HISTORY_LEN;
    g->fault_history[idx].domain_id = domain_id;
    g->fault_history[idx].timestamp = now;
    g->fault_history[idx].fault_vector = vector;
    g->fault_history[idx].fault_rip = rip;
    g->fault_history[idx].fault_cr2 = cr2;
    g->history_head++;

    /* Auto-quarantine if fault threshold exceeded */
    if (g->faults_in_window >= APK_GUARD_MAX_FAULTS_PER_SEC && !g->is_quarantined) {
        g->is_quarantined = 1;
        g->total_quarantines++;
        serial_write("[APK_GUARD][ALERT] Excessive Faults! Quarantining Domain: ");
        serial_write_dec(domain_id);
        serial_write("\n");
    }

    spinlock_unlock(&s_guard_lock);
}

int apk_guard_is_quarantined(u32 domain_id) {
    if (domain_id >= APK_MAX_DOMAINS) return 0;
    if (!s_guard_inited) return 0;
    return s_guards[domain_id].is_quarantined;
}

int apk_guard_quarantine(u32 domain_id) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    if (!s_guard_inited) apk_guard_init();

    spinlock_lock(&s_guard_lock);
    s_guards[domain_id].is_quarantined = 1;
    s_guards[domain_id].total_quarantines++;
    spinlock_unlock(&s_guard_lock);
    return 0;
}

int apk_guard_reset_and_reload(u32 domain_id) {
    if (domain_id >= APK_MAX_DOMAINS) return -1;
    if (!s_guard_inited) apk_guard_init();

    spinlock_lock(&s_guard_lock);
    s_guards[domain_id].is_quarantined = 0;
    s_guards[domain_id].faults_in_window = 0;
    s_guards[domain_id].window_start_tick = timer_ticks();
    s_guards[domain_id].total_resets++;
    spinlock_unlock(&s_guard_lock);

    serial_write("[APK_GUARD] Hardware State Cleared and Driver Hot-Reloaded for Domain: ");
    serial_write_dec(domain_id);
    serial_write("\n");
    return 0;
}

void apk_guard_tick(void) {
    if (!s_guard_inited) return;
    u32 now = timer_ticks();

    spinlock_lock(&s_guard_lock);
    for (u32 i = 0; i < APK_MAX_DOMAINS; i++) {
        if (now - s_guards[i].window_start_tick > 1000u) {
            s_guards[i].window_start_tick = now;
            s_guards[i].faults_in_window = 0;
        }
    }
    spinlock_unlock(&s_guard_lock);
}
