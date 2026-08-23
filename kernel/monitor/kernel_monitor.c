#include "kernel_monitor.h"
#include "security.h"
#include "audit.h"

static u32 security_score = 100;

void kernel_monitor_init(void) {
    security_score = 100;

    const security_profile_t* sp = security_profile();
    if (!sp->nx_active) {
        security_score -= 15;
    }
    if (!sp->smep_active) {
        security_score -= 10;
    }
    if (!sp->smap_active) {
        security_score -= 10;
    }
    if (!sp->guard_pages_enabled) {
        security_score -= 20;
    }
}

void kernel_monitor_tick(void) {
    u32 v = audit_violation_count();
    u32 penalty = v / 8;
    if (penalty > 60) {
        penalty = 60;
    }
    security_score = 100;
    const security_profile_t* sp = security_profile();
    if (!sp->nx_active) {
        security_score -= 15;
    }
    if (!sp->smep_active) {
        security_score -= 10;
    }
    if (!sp->smap_active) {
        security_score -= 10;
    }
    if (!sp->guard_pages_enabled) {
        security_score -= 20;
    }
    if (security_score > penalty) {
        security_score -= penalty;
    } else {
        security_score = 0;
    }
}

u32 kernel_monitor_security_score(void) {
    return security_score;
}
