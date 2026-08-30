#ifndef APK_FAULT_INJECT_H
#define APK_FAULT_INJECT_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u32 total_injections;
    u32 successful_heals;
    u32 failed_heals;
    u32 avg_recovery_latency_us;
} apk_fault_inject_stats_t;

int  apk_fault_inject_init(void);
int  apk_fault_inject_memory_corruption(u32 domain_id);
int  apk_fault_inject_stress(u32 domain_id, u32 iterations);
void apk_fault_inject_get_stats(apk_fault_inject_stats_t* out_stats);

#ifdef __cplusplus
}
#endif

#endif /* APK_FAULT_INJECT_H */
