#ifndef COMPAT_SECE_H
#define COMPAT_SECE_H

#include "compat.h"
#include "types.h"

typedef struct {
    u32 record_count;
    u32 quarantined_count;
} sece_stats_t;

typedef struct {
    u8 valid;
    u32 success;
    u32 crashes;
    i32 perf_delta;
    u32 trust;
} sece_profile_t;

void compat_sece_init(void);
void compat_sece_tick(void);
void compat_sece_success(u32 bbf_id, compat_personality_t personality);
void compat_sece_failure(u32 bbf_id, compat_personality_t personality);
void compat_sece_crash(u32 bbf_id, compat_personality_t personality, u32 reason);
int compat_sece_profile(u32 bbf_id, compat_personality_t personality, sece_profile_t* out);
void compat_sece_stats(sece_stats_t* out);

#endif

