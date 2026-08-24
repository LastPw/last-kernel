#ifndef NETSTACK_H
#define NETSTACK_H

#include "types.h"

typedef struct {
    u32 ip;
    u8 mac[6];
    u32 last_seen_tick;
    u8 valid;
} net_arp_entry_t;

typedef struct {
    u32 network;
    u32 mask;
    u32 gateway;
    u32 metric;
    u8 valid;
} net_route_entry_t;

typedef struct {
    u32 id;
    u32 proto;
    u16 local_port;
    u16 remote_port;
    u32 remote_ip;
    u8 state;
    u8 valid;
    u16 rx_pending;
    u32 rx_dropped;
    u32 tx_seq;
    u32 rx_seq;
    u32 pending_seq;
    u16 pending_len;
    u8 tx_retries;
    u8 tx_pending;
    u16 dup_acks;
    u32 rto_ticks;
    u32 srtt_ticks;
    u32 rttvar_ticks;
    u32 cwnd_bytes;
    u32 ssthresh_bytes;
    u32 tx_timestamp;
    u8 ipv6;
    u8 remote_ipv6[16];
    u32 parent_id;
    u32 owner_task;
} net_socket_info_t;

#define NET_SOCKET_SYN_SENT 1u
#define NET_SOCKET_ESTABLISHED 2u
#define NET_SOCKET_CLOSING 3u
#define NET_SOCKET_LISTEN 4u
#define NET_SOCKET_SYN_RECEIVED 5u

typedef struct {
    u32 frames_rx;
    u32 ipv4_rx;
    u32 arp_rx;
    u32 malformed;
    u32 arp_entries;
    u32 route_entries;
    u32 sockets_open;
    u32 local_ip;
    u32 local_mask;
    u32 local_gw;
    u8 local_ipv6[16];
    u32 ipv6_rx;
    u32 ndp_entries;
} netstack_status_t;

void netstack_init(void);
void netstack_tick(void);
void netstack_poll(u32 budget);
void netstack_on_frame(const u8* frame, u16 len);
/* Inject a raw IPv4 datagram (no Ethernet header) — used by the
 * WireGuard decapsulator. Bypasses NAT forwarding. */
void netstack_ipv4_input(const u8 src_mac[6], const u8* datagram, u16 dgram_len);
void netstack_set_local(u32 ip, u32 mask, u32 gateway);
int netstack_ipv6_local(u8 out_addr[16]);
int netstack_ipv6_ping(const u8 remote[16], u16 sequence, u32 timeout_ticks, u32* out_ticks);
void netstack_status(netstack_status_t* out);
u32 netstack_arp_count(void);
int netstack_arp_get(u32 index, net_arp_entry_t* out);
int netstack_arp_resolve(u32 ip, u32 timeout_ticks);
int netstack_prepare_peer(u32 remote_ip, u32 timeout_ticks);
int netstack_ping(u32 remote_ip, u16 sequence, u32 timeout_ticks, u32* out_ticks, u8* out_ttl);
int netstack_traceroute_probe(u32 remote_ip, u8 hop_limit, u16 sequence, u32 timeout_ticks,
                              u32* out_hop_ip, u32* out_ticks, u8* out_reached);
u32 netstack_route_count(void);
int netstack_route_get(u32 index, net_route_entry_t* out);
int netstack_route_add(u32 network, u32 mask, u32 gateway, u32 metric);
int netstack_socket_open(u32 proto, u16 local_port, u32 remote_ip, u16 remote_port);
int netstack_socket_open6(u32 proto, u16 local_port, const u8 remote_ip[16], u16 remote_port);
int netstack_socket_listen(u16 local_port);
int netstack_socket_accept(u32 listener_id, u32* out_client_id, u32* out_remote_ip, u16* out_remote_port);
int netstack_socket_close(u32 sock_id);
int netstack_socket_send(u32 sock_id, const u8* data, u16 len);
int netstack_socket_recv(u32 sock_id, u8* out, u16 out_len, u32* out_src_ip, u16* out_src_port);
int netstack_socket_recv6(u32 sock_id, u8* out, u16 out_len, u8 out_src_ipv6[16], u16* out_src_port);
u32 netstack_socket_count(void);
int netstack_socket_get(u32 index, net_socket_info_t* out);
int netstack_socket_state(u32 sock_id, u8* out_state);
int netstack_socket_set_owner(u32 sock_id, u32 owner_task);
u32 netstack_socket_owner(u32 sock_id);
void netstack_socket_release_task(u32 task_id);
u32 netstack_local_ip(void);

u32 netstack_local_mask(void);
u32 netstack_local_gw(void);

#endif
