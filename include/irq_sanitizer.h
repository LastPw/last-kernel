#ifndef IRQ_SANITIZER_H
#define IRQ_SANITIZER_H

#include "types.h"

void irq_sanitizer_init(void);
int irq_sanitizer_allow(u8 irq, u32 global_irq_count);

#endif
