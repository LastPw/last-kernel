#ifndef KTRACE_H
#define KTRACE_H

#include "types.h"

#define KTRACE_RING_SIZE 512u

typedef struct {
    u32 tick;
    u16 type;
    u16 cpu;
    u32 a0;
    u32 a1;
    u32 a2;
} ktrace_event_t;

typedef enum {
    KTRACE_EVT_NONE = 0,
    KTRACE_EVT_SYSCALL = 1,
    KTRACE_EVT_PAGE_FAULT = 2,
    KTRACE_EVT_SCHED_SWITCH = 3,
    KTRACE_EVT_MODULE_LOAD = 4,
    KTRACE_EVT_MODULE_FAIL = 5
} ktrace_type_t;

void ktrace_init(void);
void ktrace_push(u16 type, u16 cpu, u32 a0, u32 a1, u32 a2);
u32 ktrace_count(void);
int ktrace_get(u32 reverse_index, ktrace_event_t* out);

#endif
