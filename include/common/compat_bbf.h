#ifndef COMPAT_BBF_H
#define COMPAT_BBF_H

#include "compat.h"
#include "types.h"

typedef struct {
    u32 bbf_id;
    u32 cluster_id;
    u32 confidence;
} bbf_bind_result_t;

typedef struct {
    u8 valid;
    u32 bbf_id;
    u32 cluster_id;
    u32 confidence;
    u32 exec_count;
    u32 last_motif_hash;
} bbf_profile_t;

typedef struct {
    u32 profile_count;
    u32 cluster_count;
} bbf_stats_t;

void compat_bbf_init(void);
bbf_bind_result_t compat_bbf_bind_task(u32 task_id, compat_personality_t personality, u32 seed);
void compat_bbf_note_transition(u32 task_id, u16 from_nr, u16 to_nr, compat_personality_t personality);
int compat_bbf_profile(u32 task_id, bbf_profile_t* out);
u32 compat_bbf_id_of(u32 task_id);
void compat_bbf_stats(bbf_stats_t* out);

#endif

