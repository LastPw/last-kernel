#ifndef CORE_H
#define CORE_H

#include "types.h"

void core_init(void);
u32 core_tick_count(void);
u8 core_boot_mode(void);
void core_capture_trap(u32 int_no, u32 err_code, u32 eip, u32 addr_hint);

#endif
