#ifndef SWAP_H
#define SWAP_H

#include "types.h"

void swap_init(u32 max_kb);
void swap_init_scaled(u32 total_ram_bytes);
void swap_balance(u32 total_bytes, u32 used_bytes);
u32 swap_used_kb(void);
u32 swap_max_kb(void);

#endif
