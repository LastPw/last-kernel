#ifndef IOLAB_H
#define IOLAB_H

#include "types.h"

typedef struct {
    u32 samples;
    u32 window;
    u32 io_ops_total;
    u32 io_queue_depth_avg;
    u32 io_queue_depth_peak;
    u32 io_drop_total;
    u32 net_packets_total;
    u32 net_frames_rx;
    u32 net_malformed_total;
    u32 net_sockets_open;
    u32 mode_hint;
    u32 throughput_score;
    u32 latency_score;
    u32 pressure_score;
    u32 risk_score;
    u32 health_score;
    u32 last_tick;
} iolab_snapshot_t;

typedef struct {
    u32 checks_total;
    u32 checks_passed;
    u32 checks_failed;
    u32 first_error;
} iolab_validation_t;

typedef struct {
    u32 mode;
    u32 rounds;
    u32 ops_before;
    u32 ops_after;
    u32 drops_before;
    u32 drops_after;
    u32 packets_before;
    u32 packets_after;
    u32 pressure_before;
    u32 pressure_after;
    u32 failures;
} iolab_run_result_t;

void iolab_init(void);
void iolab_reset(void);
void iolab_tick(void);
void iolab_snapshot(iolab_snapshot_t* out);
int iolab_validate(iolab_validation_t* out);
int iolab_run(u32 mode, u32 rounds, u32 payload, iolab_run_result_t* out);

#endif
