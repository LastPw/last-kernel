#ifndef SERVER_NET_H
#define SERVER_NET_H

#include "types.h"

#define RSS_MAX_QUEUES          16
#define RSS_KEY_SIZE            40
#define RSS_INDIRECTION_SIZE    128

typedef enum {
    TCP_CC_CUBIC    = 0,
    TCP_CC_BBRV3    = 1,
    TCP_CC_RENO     = 2
} tcp_congestion_algo_t;

typedef struct {
    u32 src_ip;
    u32 dst_ip;
    u16 src_port;
    u16 dst_port;
    u8  protocol;
} net_flow_4tuple_t;

typedef struct {
    u8  rss_enabled;
    u32 num_rx_queues;
    u32 num_tx_queues;
    u8  rss_key[RSS_KEY_SIZE];
    u8  indirection_table[RSS_INDIRECTION_SIZE];
    u64 queue_packets[RSS_MAX_QUEUES];
} rss_config_t;

typedef struct {
    u32 min_rtt_us;
    u32 max_bw_bps;
    u32 pacing_rate_bps;
    u32 cwnd_bytes;
    u32 in_flight_bytes;
    u8  bbr_state;  /* 0=Startup, 1=Drain, 2=ProbeBW, 3=ProbeRTT */
} bbrv3_state_t;

typedef struct {
    u8  syn_cookies_enabled;
    u32 syn_cookies_sent;
    u32 syn_cookies_validated;
    u32 syn_cookies_failed;
    u32 syn_secret[4];
} syn_cookie_engine_t;

/* Server Net Subsystem API */
void server_net_init(void);

/* Receive Side Scaling (RSS) */
u32 rss_calculate_toeplitz_hash(const net_flow_4tuple_t* flow, const u8* key);
u32 rss_dispatch_queue(const net_flow_4tuple_t* flow);

/* Cryptographic TCP SYN Cookies for DDoS Protection */
u32 syn_cookie_generate(u32 saddr, u32 daddr, u16 sport, u16 dport, u32 sseq, u16 mss_idx);
int syn_cookie_check(u32 saddr, u32 daddr, u16 sport, u16 dport, u32 ack_seq, u16* mss_out);

/* BBRv3 Server Congestion Control */
void bbrv3_init_socket(bbrv3_state_t* bbr);
void bbrv3_on_ack(bbrv3_state_t* bbr, u32 rtt_sample_us, u32 bytes_acked);
u32 bbrv3_get_cwnd(const bbrv3_state_t* bbr);
u32 bbrv3_get_pacing_rate(const bbrv3_state_t* bbr);

/* Zero-Copy Server Transfer */
int server_zero_copy_splice(i32 in_fd, u64 in_offset, i32 out_fd, u64 out_offset, u64 len);

#endif /* SERVER_NET_H */
