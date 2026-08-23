#ifndef IO_SCHEDULER_H
#define IO_SCHEDULER_H

#include "types.h"

typedef enum {
    IO_MODE_LATENCY = 0,
    IO_MODE_THROUGHPUT = 1,
    IO_MODE_ADAPTIVE = 2
} io_mode_t;

typedef struct {
    u32 queue_depth;
    u32 ops_total;
    u32 bytes_total;
    u32 dropped_ops;
    u32 net_packets;
    io_mode_t mode;
} io_stats_t;

void io_scheduler_init(void);
void io_scheduler_set_mode(io_mode_t mode);
io_mode_t io_scheduler_mode(void);
int io_scheduler_submit(u32 task_id, u32 bytes, int is_read);
void io_scheduler_tick(u32 now_tick);
void io_scheduler_stats(io_stats_t* out);
void io_scheduler_account_net_packet(void);

#endif
