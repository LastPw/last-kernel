#ifndef PCOE2_CORE_H
#define PCOE2_CORE_H

#include "resource_monitor.h"
#include "types.h"

typedef struct {
    u32 ewma_cpu;
    u32 ewma_mem;
    u32 ewma_ioq;
    u32 ewma_net;
    u32 ewma_thermal;
    u32 burst_cpu;
    u32 burst_io;
    u32 burst_net;
    u32 anomaly_score;
    u32 stability_score;
    u32 predicted_cpu;
    u32 predicted_mem;
    u32 predicted_ioq;
    u32 predicted_net;
} pcoe2_core_view_t;

void pcoe2_core_init(void);
void pcoe2_core_ingest(const resource_snapshot_t* rs);
void pcoe2_core_view(pcoe2_core_view_t* out);
u32 pcoe2_core_predict_pressure(void);
u32 pcoe2_core_predict_latency_shadow(void);
u32 pcoe2_core_recommended_quantum(void);
u32 pcoe2_core_recommended_threads(u32 fallback_threads, u32 max_threads);

#endif

