#ifndef APK_AI_SCHED_H
#define APK_AI_SCHED_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 input_events_per_sec;
    u32 io_ops_per_sec;
    u32 cpu_busy_pct;
    u32 cache_miss_score;
    u32 thread_queue_depth;
} apk_ai_features_t;

typedef struct {
    u32 recommended_morph_mode;
    u32 quantum_adjustment_ms;
    u32 fastlane_priority_boost;
    u32 frame_drop_risk_pct;
    u32 confidence_pct;
} apk_ai_inference_t;

int  apk_ai_sched_init(void);
void apk_ai_sched_infer(const apk_ai_features_t* features, apk_ai_inference_t* out_inference);
void apk_ai_sched_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* APK_AI_SCHED_H */
