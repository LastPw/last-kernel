#ifndef APK_GUARD_H
#define APK_GUARD_H

#include "types.h"
#include "idt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APK_GUARD_MAX_FAULTS_PER_SEC 5
#define APK_GUARD_HISTORY_LEN 16

typedef struct {
    u32 domain_id;
    u32 timestamp;
    u64 fault_vector;
    u64 fault_rip;
    u64 fault_cr2;
} apk_fault_event_t;

typedef struct {
    u32 domain_id;
    u32 window_start_tick;
    u32 faults_in_window;
    u8  is_quarantined;
    u32 total_quarantines;
    u32 total_resets;
    apk_fault_event_t fault_history[APK_GUARD_HISTORY_LEN];
    u32 history_head;
} apk_domain_guard_t;

int  apk_guard_init(void);
void apk_guard_record_fault(u32 domain_id, u64 vector, u64 rip, u64 cr2);
int  apk_guard_is_quarantined(u32 domain_id);
int  apk_guard_quarantine(u32 domain_id);
int  apk_guard_reset_and_reload(u32 domain_id);
void apk_guard_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* APK_GUARD_H */
