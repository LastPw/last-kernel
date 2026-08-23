#include "rate_limiter.h"

static u32 configured_rate = 100;
static u32 hz = 100;
static u32 bucket_q16 = 0;
static u32 per_tick_q16 = 0;
static u32 max_bucket_q16 = 0;

void rate_limiter_init(u32 packets_per_sec, u32 timer_hz) {
    if (packets_per_sec == 0) {
        packets_per_sec = 100;
    }
    if (timer_hz == 0) {
        timer_hz = 100;
    }

    configured_rate = packets_per_sec;
    hz = timer_hz;
    per_tick_q16 = (configured_rate << 16) / hz;
    if (per_tick_q16 == 0) {
        per_tick_q16 = 1;
    }
    max_bucket_q16 = (configured_rate << 16);
    bucket_q16 = max_bucket_q16;
}

u32 rate_limiter_take_budget(void) {
    bucket_q16 += per_tick_q16;
    if (bucket_q16 > max_bucket_q16) {
        bucket_q16 = max_bucket_q16;
    }

    u32 budget = bucket_q16 >> 16;
    if (budget > 64) {
        budget = 64;
    }
    bucket_q16 -= (budget << 16);
    return budget;
}
