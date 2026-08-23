#ifndef IO_GUARD_H
#define IO_GUARD_H

#include "types.h"

void io_guard_init(void);
void io_guard_on_irq(u8 irq, u32 irq_count);
int io_guard_filter_input(int ch);

#endif
