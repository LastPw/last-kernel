#ifndef QUANTUM_SCHEDULER_H
#define QUANTUM_SCHEDULER_H

#include "types.h"
#include "resource_monitor.h"
#include "resource_engine.h"
#include "hal.h"
#include "ai_security.h"
#include "antivirus.h"

typedef struct {
    u32 score;
    u32 predicted_pressure;
    u32 preempt_hint;
    u32 quantum_level;
    u32 dependency_depth;
    u32 clone_level;
    u32 micro_thread_level;
    u32 predicted_threads;
    u32 risk_index;
    u32 stability;
    u32 anomaly;
    u32 throughput_gain;
    u32 fairness_index;
} quantum_scheduler_status_t;

void quantum_scheduler_init(u32 total_cores);
void quantum_scheduler_tick(const resource_snapshot_t* rs,
                            const resource_engine_snapshot_t* re,
                            const hal_status_t* hs,
                            const ai_security_status_t* ai,
                            const antivirus_status_t* av,
                            u32 mode);
u32 quantum_scheduler_score(void);
u32 quantum_scheduler_preempt_hint(void);
u32 quantum_scheduler_quantum_level(void);
u32 quantum_scheduler_dependency_depth(void);
u32 quantum_scheduler_clone_level(void);
u32 quantum_scheduler_micro_thread_level(void);
u32 quantum_scheduler_predicted_threads(void);
u32 quantum_scheduler_anomaly(void);
u32 quantum_scheduler_stability(void);
void quantum_scheduler_status(quantum_scheduler_status_t* out);

#endif
