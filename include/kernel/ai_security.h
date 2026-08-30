#ifndef AI_SECURITY_H
#define AI_SECURITY_H

#include "types.h"

typedef struct {
    u32 risk_score;
    u32 predicted_threat_window;
    u32 anomaly_events;
    u32 quarantined_suggestions;
    u32 adaptive_firewall_level;
    u32 learning_epoch;
    u32 desktop_mode;
} ai_security_status_t;

void ai_security_init(void);
void ai_security_tick(void);
void ai_security_on_threat(u32 threat_type, u32 task_id, u32 arg0, u32 arg1);
void ai_security_status(ai_security_status_t* out);

#endif
