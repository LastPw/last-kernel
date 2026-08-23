#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include "types.h"

void rate_limiter_init(u32 packets_per_sec, u32 timer_hz);
u32 rate_limiter_take_budget(void);

#endif
