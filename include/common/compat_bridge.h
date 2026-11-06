#ifndef COMPAT_BRIDGE_H
#define COMPAT_BRIDGE_H

#include "compat.h"
#include "types.h"

typedef struct {
    u32 translate_hits;
    u32 translate_miss;
    u32 chain_promotions;
    u32 chain_evictions;
    u32 profile_count;
    u32 avg_confidence;
} compat_bridge_stats_t;

typedef struct {
    u8 valid;
    u32 confidence;
    u32 chain_depth;
    u32 fast_path_hits;
    u32 slow_path_hits;
    u32 source_tag;
    u32 target_tag;
    u32 fingerprint;
} compat_bridge_profile_t;

typedef struct {
    u8 translated;
    u8 fast_path;
    u16 native_nr;
    u32 confidence;
    u32 source_tag;
    u32 target_tag;
    u32 chain_key;
} compat_bridge_result_t;

void compat_bridge_init(void);
void compat_bridge_tick(void);
void compat_bridge_bind(u32 task_id, compat_personality_t personality, u32 seed, u32 dll_hash);
void compat_bridge_note_crash(u32 task_id, u32 reason);
int compat_bridge_translate(u32 task_id,
                            u32 foreign_nr,
                            u32 a0,
                            u32 a1,
                            u32 a2,
                            compat_bridge_result_t* out);
u32 compat_bridge_confidence(u32 task_id);
int compat_bridge_profile(u32 task_id, compat_bridge_profile_t* out);
void compat_bridge_stats(compat_bridge_stats_t* out);

#endif
