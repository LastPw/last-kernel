#ifndef APK_MORPH_H
#define APK_MORPH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APK_MORPH_BALANCED    0u
#define APK_MORPH_THROUGHPUT  1u
#define APK_MORPH_LOW_LATENCY 2u
#define APK_MORPH_HARDENED    3u
#define APK_MORPH_GREEN       4u

typedef struct {
    u32 target_quantum_ticks;
    u32 fastlane_boost_pct;
    u32 cfs_latency_target_ms;
    u32 io_burst_credit;
    u32 power_cstate_limit;
    u8  strict_security_barriers;
} apk_sched_profile_t;

typedef struct {
    u32 active_mode;
    u32 total_switches;
    u32 input_burst_score;
    u32 io_throughput_score;
    u32 cpu_compute_score;
    apk_sched_profile_t current_profile;
} apk_morph_state_t;

int  apk_morph_init(void);
int  apk_morph_set_mode(u32 mode);
u32  apk_morph_get_mode(void);
const char* apk_morph_get_mode_name(u32 mode);

void apk_morph_feed_telemetry(u32 input_events, u32 io_ops, u32 cpu_busy_pct);
void apk_morph_get_state(apk_morph_state_t* out);

#ifdef __cplusplus
}
#endif

#endif /* APK_MORPH_H */
