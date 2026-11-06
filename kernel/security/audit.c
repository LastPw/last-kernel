#include "audit.h"
#include "spinlock.h"

#define AUDIT_CAP 128

typedef struct {
    u32 evt;
    u32 a0;
    u32 a1;
} audit_rec_t;

static audit_rec_t ring[AUDIT_CAP] __attribute__((aligned(64)));
static u32 head = 0;
static u32 violations = 0;
static spinlock_t lock;

void audit_init(void) {
    spinlock_init(&lock);
    u32 irqf = spinlock_lock_irqsave(&lock);
    head = 0;
    violations = 0;
    for (u32 i = 0; i < AUDIT_CAP; i++) {
        ring[i].evt = AUDIT_EVT_NONE;
        ring[i].a0 = 0;
        ring[i].a1 = 0;
    }
    spinlock_unlock_irqrestore(&lock, irqf);
}

void audit_log(audit_event_t evt, u32 a0, u32 a1) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    u32 idx = head % AUDIT_CAP;
    ring[idx].evt = (u32)evt;
    ring[idx].a0 = a0;
    ring[idx].a1 = a1;
    head++;

    if (evt == AUDIT_EVT_EXCEPTION || evt == AUDIT_EVT_PAGE_FAULT || evt == AUDIT_EVT_GUARD_HIT ||
        evt == AUDIT_EVT_STACK_OVERFLOW || evt == AUDIT_EVT_IRQ_RATE_LIMIT || evt == AUDIT_EVT_TASK_VIOLATION ||
        evt == AUDIT_EVT_HEAP_CORRUPTION || evt == AUDIT_EVT_BOOT_FALLBACK) {
        violations++;
    }
    spinlock_unlock_irqrestore(&lock, irqf);
}

u32 audit_violation_count(void) {
    u32 irqf = spinlock_lock_irqsave(&lock);
    u32 v = violations;
    spinlock_unlock_irqrestore(&lock, irqf);
    return v;
}
