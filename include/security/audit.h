#ifndef AUDIT_H
#define AUDIT_H

#include "types.h"

typedef enum {
    AUDIT_EVT_NONE = 0,
    AUDIT_EVT_EXCEPTION = 1,
    AUDIT_EVT_PAGE_FAULT = 2,
    AUDIT_EVT_GUARD_HIT = 3,
    AUDIT_EVT_STACK_OVERFLOW = 4,
    AUDIT_EVT_IRQ_RATE_LIMIT = 5,
    AUDIT_EVT_TASK_VIOLATION = 6,
    AUDIT_EVT_PATCH_FAIL = 7,
    AUDIT_EVT_PATCH_OK = 8,
    AUDIT_EVT_HEAP_CORRUPTION = 9,
    AUDIT_EVT_BOOT_FALLBACK = 10
} audit_event_t;

void audit_init(void);
void audit_log(audit_event_t evt, u32 a0, u32 a1);
u32 audit_violation_count(void);

#endif
