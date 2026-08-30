#ifndef SERVER_PROFILE_H
#define SERVER_PROFILE_H

#include "types.h"

typedef enum {
    KERNEL_PROFILE_DESKTOP  = 0,
    KERNEL_PROFILE_SERVER   = 1,
    KERNEL_PROFILE_EMBEDDED = 2
} kernel_profile_t;

typedef struct {
    kernel_profile_t profile;
    u32 uptime_seconds;
    u64 total_requests_served;
    u64 network_bytes_in;
    u64 network_bytes_out;
    u64 storage_read_iops;
    u64 storage_write_iops;
    u32 active_tcp_sessions;
    u32 active_micro_containers;
    u32 numa_local_hit_rate_pct;
} server_telemetry_t;

/* Server Profile API */
void server_profile_init(void);
void server_profile_set(kernel_profile_t prof);
kernel_profile_t server_profile_get(void);
u8 server_profile_is_server(void);

void server_profile_update_metrics(u64 net_in, u64 net_out, u64 rd_iops, u64 wr_iops);
void server_profile_get_telemetry(server_telemetry_t* out);

/* Prometheus / OpenMetrics In-Kernel Format Exporter */
u32 server_export_prometheus_metrics(char* buffer, u32 max_len);

#endif /* SERVER_PROFILE_H */
