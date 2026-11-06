#include "irq_sanitizer.h"

#define IRQ_LINES 16
#define RATE_WINDOW 200
#define RATE_LIMIT  120

typedef struct {
    u32 window_base;
    u32 count;
} irq_rate_t;

static irq_rate_t lines[IRQ_LINES];

void irq_sanitizer_init(void) {
    for (u32 i = 0; i < IRQ_LINES; i++) {
        lines[i].window_base = 0;
        lines[i].count = 0;
    }
}

int irq_sanitizer_allow(u8 irq, u32 global_irq_count) {
    if (irq >= IRQ_LINES) {
        return 0;
    }

    /* IRQ0 is the scheduler/watchdog clock source: dropping it starves the
       scheduler (kshield already exempts it for this reason). Rate-limit
       device IRQs only. */
    if (irq == 0u) {
        return 1;
    }

    irq_rate_t* r = &lines[irq];

    if (global_irq_count - r->window_base > RATE_WINDOW) {
        r->window_base = global_irq_count;
        r->count = 0;
    }

    r->count++;
    if (r->count > RATE_LIMIT) {
        return 0;
    }

    return 1;
}
