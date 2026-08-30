#ifndef COMPAT_ASTG_H
#define COMPAT_ASTG_H

#include "compat.h"
#include "types.h"

typedef struct {
    u8 promoted;
    u8 quarantined;
    u16 reserved;
    u32 hot_score;
} astg_feedback_t;

typedef struct {
    u32 chain_count;
    u32 edge_count;
    u32 promoted_count;
    u32 quarantined_count;
} astg_stats_t;

void compat_astg_init(void);
void compat_astg_tick(void);
astg_feedback_t compat_astg_record(compat_personality_t personality, u16 from_nr, u16 to_nr, u32 latency, u8 failed);
u16 compat_astg_predict_next(compat_personality_t personality, u16 current_nr);
void compat_astg_quarantine_personality(compat_personality_t personality);
void compat_astg_stats(astg_stats_t* out);

#endif

