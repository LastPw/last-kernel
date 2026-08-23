#ifndef COMPAT_DLLSYNC_H
#define COMPAT_DLLSYNC_H

#include "compat.h"
#include "types.h"

typedef struct {
    u32 sync_events;
    u32 desync_events;
    u32 profiles;
    u32 avg_sync_score;
} compat_dllsync_stats_t;

typedef struct {
    u8 valid;
    u32 sync_score;
    u32 drift_score;
    u32 bitmap_hits;
    u32 import_count;
    u32 export_count;
    u32 dll_hash;
} compat_dllsync_profile_t;

void compat_dllsync_init(void);
void compat_dllsync_tick(void);
void compat_dllsync_bind(u32 task_id, compat_personality_t personality, u32 seed);
void compat_dllsync_record_call(u32 task_id, u32 foreign_nr, u32 a0, u32 a1, u32 a2);
void compat_dllsync_note_crash(u32 task_id, u32 reason);
u32 compat_dllsync_sync_score(u32 task_id);
int compat_dllsync_profile(u32 task_id, compat_dllsync_profile_t* out);
void compat_dllsync_stats(compat_dllsync_stats_t* out);

#endif
