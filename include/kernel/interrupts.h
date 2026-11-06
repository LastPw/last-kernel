#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "idt.h"

typedef void (*isr_t)(registers_t* regs);

void interrupts_init(void);
void isr_register_handler(u8 n, isr_t handler);
void irq_register_handler(u8 n, isr_t handler);
void interrupts_enable(void);
void interrupts_disable(void);
u32 interrupts_total_count(void);
void interrupts_mask_all(void);
void interrupts_unmask_irq(u8 irq);
void interrupts_mask_irq(u8 irq);
void interrupts_set_minimal_mode(u8 enabled);
void interrupts_set_apic_mode(u8 enabled);
u8 interrupts_apic_mode(void);

#endif
