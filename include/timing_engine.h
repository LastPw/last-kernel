#ifndef TIMING_ENGINE_H
#define TIMING_ENGINE_H

#include "types.h"

typedef struct {
    u8 template_id;
    u32 timeout_ticks;
    u8 max_retries;
    u32 inter_probe_delay_ticks;
} timing_profile_t;

void timing_engine_build(u8 template_id, u32 timer_hz, timing_profile_t* out);

#endif
