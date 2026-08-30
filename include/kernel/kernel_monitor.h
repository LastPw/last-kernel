#ifndef KERNEL_MONITOR_H
#define KERNEL_MONITOR_H

#include "types.h"

void kernel_monitor_init(void);
void kernel_monitor_tick(void);
u32 kernel_monitor_security_score(void);

#endif
