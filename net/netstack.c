#include "netstack.h"
#include "ethernet.h"
#include "ip.h"
#include "tcp_raw.h"
#include "udp_raw.h"
#include "lastmap.h"
#include "lastshark.h"
#include "antivirus.h"
#include "threat_logger.h"
#include "timer.h"
#include "string.h"
#include "nic.h"
#include "checksum.h"
#include "ipv6.h"
#include "ipv6_transport.h"
#include "entropy.h"
#include "nat.h"
#include "dns_server.h"
#include "dhcp_server.h"

#define ARP_MAX 32u
#define ROUTE_MAX 16u
#define SOCK_MAX 32u
#define SOCK_RX_CAP 16u
#define SOCK_RX_DATA_MAX 1024u
#define TCP_MSS 1024u
#define TCP_RTO_MIN 25u
#define TCP_RTO_MAX 800u
#define TCP_INITIAL_RTO 100u
#define TCP_MAX_RETRIES 8u

typedef struct __attribute__((packed)) {
    u16 htype;
    u16 ptype;
    u8 hlen;
    u8 plen;
    u16 oper;
    u8 sha[6];
    u32 spa;
    u8 tha[6];
    u32 tpa;
} arp_header_t;

typedef struct __attribute__((packed)) {
    u8 type;
    u8 code;
    u16 checksum;
    u16 identifier;
    u16 sequence;
} icmp_echo_header_t;

typedef struct {
    u8 address[16];
    u8 mac[6];
    u32 last_seen_tick;
    u8 valid;
} ndp_entry_t;

typedef struct __attribute__((packed)) {
    u8 type;
    u8 length;
    u8 mac[6];
} ndp_ll_option_t;

typedef struct __attribute__((packed)) {
    u32 reserved;
    u8 target[16];
    ndp_ll_option_t option;
} ndp_ns_t;

typedef struct __attribute__((packed)) {
    u32 flags;
    u8 target[16];
    ndp_ll_option_t option;
} ndp_na_t;

#define ARP_HTYPE_ETHERNET 1u
#define ARP_OPER_REQUEST 1u
#define ARP_OPER_REPLY 2u
#define ARP_ENTRY_TTL 1800u
#define NDP_MAX 32u
#define NDP_ENTRY_TTL 1800u
#define ICMPV6_ECHO_REQUEST 128u
#define ICMPV6_ECHO_REPLY 129u
#define ICMPV6_NS 135u
#define ICMPV6_NA 136u
#define ICMPV6_OPT_SOURCE_LL 1u
#define ICMPV6_OPT_TARGET_LL 2u

static net_arp_entry_t g_arp[ARP_MAX];
static net_route_entry_t g_route[ROUTE_MAX];
static net_socket_info_t g_sock[SOCK_MAX];
static netstack_status_t g_st;
static u32 g_sock_next = 1u;

static u32 tcp_initial_sequence(u32 socket_id) {
    u8 random_bytes[4];
    u32 value;
    (void)socket_id;
    /* SECURITY: ISN is derived purely from the entropy pool.
     * Mixing with predictable values does not add entropy. */
    entropy_fill(random_bytes, sizeof(random_bytes));
    value = (u32)random_bytes[0] | ((u32)random_bytes[1] << 8u) |
            ((u32)random_bytes[2] << 16u) | ((u32)random_bytes[3] << 24u);
    return value ? value : 0xA5C3F17Du;
}
static ndp_entry_t g_ndp[NDP_MAX];
static u8 g_ipv6_local[16];
static u8 g_ipv6_ping_received;
static u8 g_ipv6_ping_peer[16];
static u16 g_ipv6_ping_sequence;
static u32 g_ipv6_ping_ticks;
static u8 g_ipv6_tx_frame[1514] __attribute__((aligned(16)));
static u8 g_ipv6_ndp_payload[64] __attribute__((aligned(4)));
static u8 g_ipv6_echo_reply[1200] __attribute__((aligned(4)));

static int socket_match6(u32 proto, u16 local_port, const u8 remote_ip[16], u16 remote_port);
static void socket_queue_packet6(int slot, const u8 src_ip[16], u16 src_port, const u8* data, u16 len);

typedef struct {
    u16 len;
    u16 src_port;
    u32 src_ip;
    u8 src_ipv6[16];
    u8 ipv6;
    u8 data[SOCK_RX_DATA_MAX];
} socket_rx_packet_t;

static socket_rx_packet_t g_sock_rx[SOCK_MAX][SOCK_RX_CAP];
static u8 g_sock_rx_head[SOCK_MAX];
static u8 g_sock_rx_tail[SOCK_MAX];
static u8 g_sock_rx_count[SOCK_MAX];
static u8 g_sock_tx[SOCK_MAX][SOCK_RX_DATA_MAX];
static u32 g_sock_tx_tick[SOCK_MAX];
static u8 g_ping_received = 0u;
static u32 g_ping_ip = 0u;
static u16 g_ping_sequence = 0u;
static u32 g_ping_ticks = 0u;
static u8 g_ping_ttl = 0u;
static u8 g_trace_received = 0u;
static u8 g_trace_reached = 0u;
static u32 g_trace_hop_ip = 0u;
static u16 g_trace_sequence = 0u;
static u32 g_trace_ticks = 0u;

static void socket_abort_slot(u32 slot) {
    if (slot >= SOCK_MAX || !g_sock[slot].valid) return;
    g_sock[slot].valid = 0u;
    g_sock_rx_head[slot] = 0u;
    g_sock_rx_tail[slot] = 0u;
    g_sock_rx_count[slot] = 0u;
    g_sock[slot].tx_pending = 0u;
    g_sock[slot].pending_len = 0u;
    if (g_st.sockets_open > 0u) g_st.sockets_open--;
}

static u32 tcp_clamp_rto(u32 rto) {
    if (rto < TCP_RTO_MIN) return TCP_RTO_MIN;
    if (rto > TCP_RTO_MAX) return TCP_RTO_MAX;
    return rto;
}

static void tcp_on_ack(net_socket_info_t* sk, u32 now, u32 sample_ticks) {
    if (!sk) return;
    if (sample_ticks != 0u) {
        if (sk->srtt_ticks == 0u) {
            sk->srtt_ticks = sample_ticks;
            sk->rttvar_ticks = sample_ticks / 2u;
        } else {
            u32 diff = sk->srtt_ticks > sample_ticks ? sk->srtt_ticks - sample_ticks : sample_ticks - sk->srtt_ticks;
            sk->rttvar_ticks = (3u * sk->rttvar_ticks + diff) / 4u;
            sk->srtt_ticks = (7u * sk->srtt_ticks + sample_ticks) / 8u;
        }
        sk->rto_ticks = tcp_clamp_rto(sk->srtt_ticks + 4u * sk->rttvar_ticks);
    }
    sk->tx_retries = 0u;
    sk->dup_acks = 0u;
    if (sk->cwnd_bytes < sk->ssthresh_bytes) {
        sk->cwnd_bytes += TCP_MSS;
    } else {
        u32 increase = (TCP_MSS * TCP_MSS) / (sk->cwnd_bytes ? sk->cwnd_bytes : TCP_MSS);
        sk->cwnd_bytes += increase ? increase : 1u;
    }
    if (sk->cwnd_bytes > 65535u) sk->cwnd_bytes = 65535u;
    (void)now;
}

static void log_malformed(u32 code, u32 arg) {
    g_st.malformed++;
    threat_logger_log(THREAT_IO_MALFORMED, 0, code, arg);
    antivirus_on_network_event(arg, 1);
}

static int arp_find_ip(u32 ip) {
    for (u32 i = 0; i < ARP_MAX; i++) {
        if (g_arp[i].valid && g_arp[i].ip == ip) return (int)i;
    }
    return -1;
}

static void arp_touch(u32 ip, const u8* mac) {
    int idx = arp_find_ip(ip);
    if (idx < 0) {
        for (u32 i = 0; i < ARP_MAX; i++) {
            if (!g_arp[i].valid) {
                idx = (int)i;
                g_arp[i].valid = 1u;
                g_st.arp_entries++;
                break;
            }
        }
    }
    if (idx < 0) {
        idx = 0;
        for (u32 i = 1; i < ARP_MAX; i++) {
            if (g_arp[i].last_seen_tick < g_arp[(u32)idx].last_seen_tick) idx = (int)i;
        }
    }
    g_arp[(u32)idx].ip = ip;
    for (u32 j = 0; j < 6u; j++) g_arp[(u32)idx].mac[j] = mac[j];
    g_arp[(u32)idx].last_seen_tick = timer_ticks();
}

static int arp_send_request(u32 target_ip) {
    u8 frame[sizeof(eth_header_t) + sizeof(arp_header_t)];
    eth_header_t* eth = (eth_header_t*)frame;
    arp_header_t* a = (arp_header_t*)(frame + sizeof(eth_header_t));
    u8 src[ETH_ADDR_LEN];
    ethernet_default_mac(src, 0);
    for (u32 i = 0u; i < ETH_ADDR_LEN; i++) {
        eth->src[i] = src[i];
        eth->dst[i] = 0xFFu;
        a->sha[i] = src[i];
        a->tha[i] = 0u;
    }
    eth->ethertype = ip_htons(ETH_TYPE_ARP);
    a->htype = ip_htons(ARP_HTYPE_ETHERNET);
    a->ptype = ip_htons(ETH_TYPE_IPV4);
    a->hlen = ETH_ADDR_LEN;
    a->plen = 4u;
    a->oper = ip_htons(ARP_OPER_REQUEST);
    a->spa = ip_htonl(g_st.local_ip);
    a->tpa = ip_htonl(target_ip);
    return ethernet_send_frame(frame, sizeof(frame));
}

static int arp_send_reply(const arp_header_t* request, const u8* request_mac) {
    u8 frame[sizeof(eth_header_t) + sizeof(arp_header_t)];
    eth_header_t* eth = (eth_header_t*)frame;
    arp_header_t* a = (arp_header_t*)(frame + sizeof(eth_header_t));
    u8 src[ETH_ADDR_LEN];
    if (!request || !request_mac) return -1;
    ethernet_default_mac(src, 0);
    for (u32 i = 0u; i < ETH_ADDR_LEN; i++) {
        eth->src[i] = src[i];
        eth->dst[i] = request_mac[i];
        a->sha[i] = src[i];
        a->tha[i] = request->sha[i];
    }
    eth->ethertype = ip_htons(ETH_TYPE_ARP);
    a->htype = ip_htons(ARP_HTYPE_ETHERNET);
    a->ptype = ip_htons(ETH_TYPE_IPV4);
    a->hlen = ETH_ADDR_LEN;
    a->plen = 4u;
    a->oper = ip_htons(ARP_OPER_REPLY);
    a->spa = ip_htonl(g_st.local_ip);
    a->tpa = request->spa;
    return ethernet_send_frame(frame, sizeof(frame));
}

static void handle_arp(const u8* l3, u16 l3_len) {
    if (l3_len < sizeof(arp_header_t)) {
        log_malformed(120, l3_len);
        return;
    }
    const arp_header_t* a = (const arp_header_t*)l3;
    if (ip_htons(a->htype) != 1u || ip_htons(a->ptype) != ETH_TYPE_IPV4 || a->hlen != 6u || a->plen != 4u) {
        log_malformed(121, l3_len);
        return;
    }
    u32 spa = ip_htonl(a->spa);
    arp_touch(spa, a->sha);
    if (ip_htons(a->oper) == ARP_OPER_REQUEST &&
        g_st.local_ip != 0u && ip_htonl(a->tpa) == g_st.local_ip) {
        (void)arp_send_reply(a, a->sha);
    }
    g_st.arp_rx++;
}

static int ipv6_equal(const u8 a[16], const u8 b[16]) {
    for (u32 i = 0u; i < 16u; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int ndp_find(const u8 address[16]) {
    for (u32 i = 0u; i < NDP_MAX; i++) {
        if (g_ndp[i].valid && ipv6_equal(g_ndp[i].address, address)) return (int)i;
    }
    return -1;
}

static void ndp_touch(const u8 address[16], const u8 mac[6]) {
    int idx = ndp_find(address);
    if (idx < 0) {
        for (u32 i = 0u; i < NDP_MAX; i++) {
            if (!g_ndp[i].valid) {
                idx = (int)i;
                g_ndp[i].valid = 1u;
                g_st.ndp_entries++;
                break;
            }
        }
    }
    if (idx < 0) idx = 0;
    for (u32 i = 0u; i < 16u; i++) g_ndp[(u32)idx].address[i] = address[i];
    for (u32 i = 0u; i < 6u; i++) g_ndp[(u32)idx].mac[i] = mac[i];
    g_ndp[(u32)idx].last_seen_tick = timer_ticks();
}

static void ipv6_solicited_node(const u8 target[16], u8 address[16]) {
    for (u32 i = 0u; i < 16u; i++) address[i] = 0u;
    address[0] = 0xFFu; address[1] = 0x02u; address[11] = 0x01u;
    address[12] = 0xFFu; address[13] = target[13]; address[14] = target[14]; address[15] = target[15];
}

static void ipv6_multicast_mac(const u8 address[16], u8 mac[6]) {
    mac[0] = 0x33u; mac[1] = 0x33u; mac[2] = address[12]; mac[3] = address[13];
    mac[4] = address[14]; mac[5] = address[15];
}

static int ipv6_send_icmp(const u8 dst[16], const u8 dst_mac[6], const u8* payload, u16 payload_len) {
    u8* frame = g_ipv6_tx_frame;
    eth_header_t* eth = (eth_header_t*)frame;
    ipv6_header_t* ip = (ipv6_header_t*)(frame + sizeof(eth_header_t));
    u8* body = frame + sizeof(eth_header_t) + sizeof(ipv6_header_t);
    u8 src_mac[6];
    if (!dst || !payload || payload_len > 1200u || nic_get_mac(src_mac) != 0) return -1;
    if (ipv6_build_header(ip, g_ipv6_local, dst, IPV6_NEXT_ICMP, payload_len, 255u) != 0) return -1;
    for (u32 i = 0u; i < 6u; i++) { eth->src[i] = src_mac[i]; eth->dst[i] = dst_mac[i]; }
    eth->ethertype = ip_htons(ETH_TYPE_IPV6);
    memcpy(body, payload, payload_len);
    ((icmpv6_header_t*)body)->checksum = ipv6_transport_checksum(ip->src, ip->dst, IPV6_NEXT_ICMP, body, payload_len);
    return ethernet_send_frame(frame, (u16)(sizeof(eth_header_t) + sizeof(ipv6_header_t) + payload_len));
}

static int ipv6_send_neighbor_solicitation(const u8 target[16]) {
    u8 dst[16], mac[6], src_mac[6];
    u8* payload = g_ipv6_ndp_payload;
    ndp_ns_t* ns = (ndp_ns_t*)(payload + sizeof(icmpv6_header_t));
    ipv6_solicited_node(target, dst);
    ipv6_multicast_mac(dst, mac);
    if (nic_get_mac(src_mac) != 0) return -1;
    memset(payload, 0, sizeof(g_ipv6_ndp_payload));
    payload[0] = ICMPV6_NS;
    memcpy(ns->target, target, 16u);
    ns->option.type = ICMPV6_OPT_SOURCE_LL; ns->option.length = 1u;
    memcpy(ns->option.mac, src_mac, 6u);
    return ipv6_send_icmp(dst, mac, payload, (u16)(sizeof(icmpv6_header_t) + sizeof(ndp_ns_t)));
}

static int ipv6_send_neighbor_advertisement(const u8 dst[16], const u8 dst_mac[6]) {
    u8* payload = g_ipv6_ndp_payload;
    ndp_na_t* na = (ndp_na_t*)(payload + sizeof(icmpv6_header_t));
    u8 src_mac[6];
    if (nic_get_mac(src_mac) != 0) return -1;
    memset(payload, 0, sizeof(g_ipv6_ndp_payload)); payload[0] = ICMPV6_NA;
    na->flags = ip_htonl(0x60000000u); memcpy(na->target, g_ipv6_local, 16u);
    na->option.type = ICMPV6_OPT_TARGET_LL; na->option.length = 1u;
    memcpy(na->option.mac, src_mac, 6u);
    return ipv6_send_icmp(dst, dst_mac, payload, (u16)(sizeof(icmpv6_header_t) + sizeof(ndp_na_t)));
}

static int ndp_resolve(const u8 target[16], u32 timeout_ticks, u8 out_mac[6]) {
    int idx;
    u32 deadline;
    if (!target || !out_mac) return -1;
    idx = ndp_find(target);
    if (idx >= 0 && (timer_ticks() - g_ndp[(u32)idx].last_seen_tick) <= NDP_ENTRY_TTL) {
        memcpy(out_mac, g_ndp[(u32)idx].mac, 6u);
        return 0;
    }
    if (ipv6_send_neighbor_solicitation(target) != 0) return -1;
    deadline = timer_ticks() + (timeout_ticks ? timeout_ticks : 100u);
    do {
        (void)nic_poll(8u);
        idx = ndp_find(target);
        if (idx >= 0) {
            memcpy(out_mac, g_ndp[(u32)idx].mac, 6u);
            return 0;
        }
    } while ((i32)(timer_ticks() - deadline) < 0);
    return -1;
}

static void handle_ipv6(const u8* l3, u16 l3_len, const u8 src_mac[6]) {
    const ipv6_header_t* ip;
    const u8* body;
    u16 payload_len;
    if (!l3 || l3_len < sizeof(ipv6_header_t)) { log_malformed(130, l3_len); return; }
    ip = (const ipv6_header_t*)l3;
    if ((ip_htonl(ip->version_class_flow) >> 28u) != 6u) { log_malformed(131, ip->version_class_flow); return; }
    payload_len = ip_htons(ip->payload_len);
    if (payload_len > l3_len - sizeof(ipv6_header_t)) return;
    body = l3 + sizeof(ipv6_header_t);
    g_st.ipv6_rx++;
    if (ip->next_header == IPV6_NEXT_UDP) {
        const udp_header_t* udp;
        u16 udplen, src_port, dst_port, data_len;
        int sk;
        if (payload_len < sizeof(udp_header_t)) return;
        udp = (const udp_header_t*)body;
        udplen = ip_htons(udp->len);
        if (udplen < sizeof(udp_header_t) || udplen > payload_len || udp->checksum == 0u ||
            ipv6_transport_checksum(ip->src, ip->dst, IPV6_NEXT_UDP, body, udplen) != 0u) {
            log_malformed(133, udplen); return;
        }
        src_port = ip_htons(udp->src_port); dst_port = ip_htons(udp->dst_port);
        data_len = (u16)(udplen - sizeof(udp_header_t));
        sk = socket_match6(IP_PROTO_UDP, dst_port, ip->src, src_port);
        if (sk >= 0 && data_len > 0u) {
            g_sock[(u32)sk].state = NET_SOCKET_ESTABLISHED;
            socket_queue_packet6(sk, ip->src, src_port, body + sizeof(udp_header_t), data_len);
        }
        return;
    }
    if (ip->next_header == IPV6_NEXT_TCP) {
        const tcp_header_t* tcp;
        u16 tcp_hlen, src_port, dst_port, data_len;
        u32 seq, ack;
        int sk;
        if (payload_len < sizeof(tcp_header_t)) return;
        tcp = (const tcp_header_t*)body;
        tcp_hlen = (u16)(((u16)(tcp->data_offset >> 4) & 0x0Fu) * 4u);
        if (tcp_hlen < sizeof(tcp_header_t) || tcp_hlen > payload_len ||
            ipv6_transport_checksum(ip->src, ip->dst, IPV6_NEXT_TCP, body, payload_len) != 0u) {
            log_malformed(134, tcp->data_offset); return;
        }
        src_port = ip_htons(tcp->src_port); dst_port = ip_htons(tcp->dst_port);
        seq = ip_htonl(tcp->seq); ack = ip_htonl(tcp->ack);
        data_len = (u16)(payload_len - tcp_hlen);
        sk = socket_match6(IP_PROTO_TCP, dst_port, ip->src, src_port);
        if (sk >= 0) {
            net_socket_info_t* socket = &g_sock[(u32)sk];
            if (tcp->flags & TCP_FLAG_RST) { socket_abort_slot((u32)sk); return; }
            if ((tcp->flags & TCP_FLAG_SYN) && (tcp->flags & TCP_FLAG_ACK) && socket->state == NET_SOCKET_SYN_SENT) {
                socket->tx_seq += 1u; socket->rx_seq = seq + 1u; socket->state = NET_SOCKET_ESTABLISHED;
                (void)ipv6_tcp_send(g_ipv6_local, ip->src, src_mac, dst_port, src_port,
                                    socket->tx_seq, socket->rx_seq, TCP_FLAG_ACK, 0, 0u);
            }
            if (tcp->flags & TCP_FLAG_ACK) {
                if (socket->tx_pending && ack >= socket->pending_seq + socket->pending_len) {
                    u32 sample = timer_ticks() - socket->tx_timestamp;
                    socket->tx_pending = 0u; socket->pending_len = 0u;
                    tcp_on_ack(socket, timer_ticks(), sample);
                } else if (socket->tx_pending && ack == socket->pending_seq) {
                    socket->dup_acks++;
                }
            }
            if (data_len > 0u && socket->state == NET_SOCKET_ESTABLISHED && seq == socket->rx_seq) {
                socket_queue_packet6(sk, ip->src, src_port, body + tcp_hlen, data_len);
                socket->rx_seq += data_len;
                (void)ipv6_tcp_send(g_ipv6_local, ip->src, src_mac, dst_port, src_port,
                                    socket->tx_seq, socket->rx_seq, TCP_FLAG_ACK, 0, 0u);
            }
            if ((tcp->flags & TCP_FLAG_FIN) && socket->state != 0u) {
                socket->rx_seq = seq + data_len + 1u; socket->state = NET_SOCKET_CLOSING;
                (void)ipv6_tcp_send(g_ipv6_local, ip->src, src_mac, dst_port, src_port,
                                    socket->tx_seq, socket->rx_seq, TCP_FLAG_ACK, 0, 0u);
            }
        }
        return;
    }
    if (ip->next_header != IPV6_NEXT_ICMP) return;
    if (payload_len < sizeof(icmpv6_header_t) || ipv6_transport_checksum(ip->src, ip->dst, ip->next_header,
                                                                          body, payload_len) != 0u) {
        log_malformed(132, payload_len); return;
    }
    if (body[0] == ICMPV6_NS && payload_len >= sizeof(icmpv6_header_t) + sizeof(ndp_ns_t)) {
        const ndp_ns_t* ns = (const ndp_ns_t*)(body + sizeof(icmpv6_header_t));
        if (ns->option.type == ICMPV6_OPT_SOURCE_LL && ns->option.length == 1u) ndp_touch(ip->src, ns->option.mac);
        if (ipv6_equal(ns->target, g_ipv6_local) && src_mac) {
            if (ip->src[0] || ip->src[1] || ip->src[2] || ip->src[3]) {
                (void)ipv6_send_neighbor_advertisement(ip->src, src_mac);
            }
        }
        return;
    }
    if (body[0] == ICMPV6_NA && payload_len >= sizeof(icmpv6_header_t) + sizeof(ndp_na_t)) {
        const ndp_na_t* na = (const ndp_na_t*)(body + sizeof(icmpv6_header_t));
        if (na->option.type == ICMPV6_OPT_TARGET_LL && na->option.length == 1u) ndp_touch(na->target, na->option.mac);
        return;
    }
    if (body[0] == ICMPV6_ECHO_REPLY && payload_len >= 8u && ipv6_equal(ip->src, g_ipv6_ping_peer)) {
        const u16 seq = ip_htons(*(const u16*)(body + 6u));
        if (seq == g_ipv6_ping_sequence) { g_ipv6_ping_received = 1u; g_ipv6_ping_ticks = timer_ticks(); }
        return;
    }
    if (body[0] == ICMPV6_ECHO_REQUEST && payload_len >= 8u && ipv6_equal(ip->dst, g_ipv6_local) && src_mac) {
        u8* reply = g_ipv6_echo_reply;
        if (payload_len > sizeof(g_ipv6_echo_reply)) { log_malformed(135, payload_len); return; }
        memcpy(reply, body, payload_len);
        reply[0] = ICMPV6_ECHO_REPLY;
        (void)ipv6_send_icmp(ip->src, src_mac, reply, payload_len);
    }
}

static int route_match(u32 ip, const net_route_entry_t* r) {
    return r->valid && ((ip & r->mask) == (r->network & r->mask));
}

static int socket_slot_by_id(u32 id) {
    for (u32 i = 0; i < SOCK_MAX; i++) {
        if (g_sock[i].valid && g_sock[i].id == id) return (int)i;
    }
    return -1;
}

static int socket_match(u32 proto, u16 local_port, u32 remote_ip, u16 remote_port) {
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (!g_sock[i].valid || g_sock[i].proto != proto) continue;
        if (g_sock[i].state == NET_SOCKET_LISTEN) continue;
        if (g_sock[i].local_port != local_port) continue;
        if (g_sock[i].remote_ip != 0u && g_sock[i].remote_ip != remote_ip) continue;
        if (g_sock[i].remote_port != 0u && g_sock[i].remote_port != remote_port) continue;
        return (int)i;
    }
    return -1;
}

static int socket_listener(u32 proto, u16 local_port) {
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (g_sock[i].valid && g_sock[i].proto == proto && g_sock[i].state == NET_SOCKET_LISTEN &&
            g_sock[i].local_port == local_port) return (int)i;
    }
    return -1;
}

static int socket_match6(u32 proto, u16 local_port, const u8 remote_ip[16], u16 remote_port) {
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (!g_sock[i].valid || !g_sock[i].ipv6 || g_sock[i].proto != proto) continue;
        if (g_sock[i].local_port != local_port ||
            (g_sock[i].remote_port != 0u && g_sock[i].remote_port != remote_port)) continue;
        if (!ipv6_equal(g_sock[i].remote_ipv6, remote_ip)) continue;
        return (int)i;
    }
    return -1;
}

static void socket_queue_packet(int slot, u32 src_ip, u16 src_port, const u8* data, u16 len) {
    if (slot < 0 || slot >= (int)SOCK_MAX || !data || len == 0u) return;
    if (len > SOCK_RX_DATA_MAX) len = SOCK_RX_DATA_MAX;
    if (g_sock_rx_count[(u32)slot] >= SOCK_RX_CAP) {
        g_sock[(u32)slot].rx_dropped++;
        return;
    }
    u32 q = g_sock_rx_tail[(u32)slot];
    g_sock_rx[(u32)slot][q].src_ip = src_ip;
    g_sock_rx[(u32)slot][q].src_port = src_port;
    g_sock_rx[(u32)slot][q].len = len;
    memcpy(g_sock_rx[(u32)slot][q].data, data, len);
    g_sock_rx_tail[(u32)slot] = (u8)((q + 1u) % SOCK_RX_CAP);
    g_sock_rx_count[(u32)slot]++;
    g_sock[(u32)slot].rx_pending = g_sock_rx_count[(u32)slot];
}

static void socket_queue_packet6(int slot, const u8 src_ip[16], u16 src_port, const u8* data, u16 len) {
    if (slot < 0 || slot >= (int)SOCK_MAX || !src_ip || !data || len == 0u) return;
    if (len > SOCK_RX_DATA_MAX) len = SOCK_RX_DATA_MAX;
    if (g_sock_rx_count[(u32)slot] >= SOCK_RX_CAP) { g_sock[(u32)slot].rx_dropped++; return; }
    u32 q = g_sock_rx_tail[(u32)slot];
    g_sock_rx[(u32)slot][q].src_ip = 0u;
    memcpy(g_sock_rx[(u32)slot][q].src_ipv6, src_ip, 16u);
    g_sock_rx[(u32)slot][q].ipv6 = 1u;
    g_sock_rx[(u32)slot][q].src_port = src_port;
    g_sock_rx[(u32)slot][q].len = len;
    memcpy(g_sock_rx[(u32)slot][q].data, data, len);
    g_sock_rx_tail[(u32)slot] = (u8)((q + 1u) % SOCK_RX_CAP);
    g_sock_rx_count[(u32)slot]++;
    g_sock[(u32)slot].rx_pending = g_sock_rx_count[(u32)slot];
}

#include "airmesh.h"
#include "wireguard.h"

void netstack_init(void) {
    g_st.frames_rx = 0u;
    g_st.ipv4_rx = 0u;
    g_st.arp_rx = 0u;
    g_st.malformed = 0u;
    g_st.arp_entries = 0u;
    g_st.route_entries = 0u;
    g_st.sockets_open = 0u;
    g_st.local_ip = 0xC0A80164u;   /* 192.168.1.100 */
    g_st.local_mask = 0xFFFFFF00u; /* /24 */
    g_st.local_gw = 0xC0A80101u;   /* 192.168.1.1 */
    g_st.ipv6_rx = 0u;
    g_st.ndp_entries = 0u;
    memset(g_ipv6_local, 0, sizeof(g_ipv6_local));
    {
        u8 mac[6];
        if (nic_get_mac(mac) == 0) (void)ipv6_link_local_from_mac(mac, g_ipv6_local);
    }
    memcpy(g_st.local_ipv6, g_ipv6_local, sizeof(g_ipv6_local));
    g_ipv6_ping_received = 0u;
    g_ping_received = 0u;

    for (u32 i = 0; i < ARP_MAX; i++) g_arp[i].valid = 0u;
    for (u32 i = 0; i < NDP_MAX; i++) g_ndp[i].valid = 0u;
    for (u32 i = 0; i < ROUTE_MAX; i++) g_route[i].valid = 0u;
    for (u32 i = 0; i < SOCK_MAX; i++) {
        g_sock[i].valid = 0u;
        g_sock_rx_head[i] = 0u;
        g_sock_rx_tail[i] = 0u;
        g_sock_rx_count[i] = 0u;
        memset(g_sock_tx[i], 0, sizeof(g_sock_tx[i]));
        g_sock_tx_tick[i] = 0u;
    }

    (void)netstack_route_add(g_st.local_ip & g_st.local_mask, g_st.local_mask, 0u, 10u);
    (void)netstack_route_add(0u, 0u, g_st.local_gw, 100u);

    airmesh_init();
    wireguard_init();
}

void netstack_poll(u32 budget) {
    (void)nic_poll(budget ? budget : 8u);
    dns_server_poll();
    dhcp_server_poll();
    netstack_tick();
    airmesh_tick();
    wireguard_tick();
}

void netstack_on_frame(const u8* frame, u16 len) {
    g_st.frames_rx++;
    lastshark_on_frame(frame, len);
    antivirus_on_network_event(len, 0);

    if (!frame || len < sizeof(eth_header_t)) {
        log_malformed(100, len);
        return;
    }

    const eth_header_t* eth = (const eth_header_t*)frame;
    u16 et = ip_htons(eth->ethertype);

    if (et == ETH_TYPE_AIRMESH) {
        airmesh_on_rx_frame(frame, len);
        return;
    }

    if (et == ETH_TYPE_ARP) {
        const u8* l3a = frame + sizeof(eth_header_t);
        u16 l3a_len = (u16)(len - sizeof(eth_header_t));
        handle_arp(l3a, l3a_len);
        return;
    }

    if (et == ETH_TYPE_IPV6) {
        /* Keep the new IPv6 parser behind the validated-frame gate while debugging. */
        if (len >= sizeof(eth_header_t) + sizeof(ipv6_header_t)) {
            handle_ipv6(frame + sizeof(eth_header_t), (u16)(len - sizeof(eth_header_t)), eth->src);
        }
        return;
    }
    if (et != ETH_TYPE_IPV4) {
        return;
    }
    g_st.ipv4_rx++;

    const u8* l3 = frame + sizeof(eth_header_t);
    u16 l3_len = (u16)(len - sizeof(eth_header_t));
    if (l3_len < sizeof(ipv4_header_t)) {
        log_malformed(101, l3_len);
        return;
    }

    const ipv4_header_t* ip = (const ipv4_header_t*)l3;
    u8 version = (u8)(ip->ver_ihl >> 4);
    u8 ihl_words = (u8)(ip->ver_ihl & 0x0F);
    u16 ihl = (u16)(ihl_words * 4u);
    if (version != 4 || ihl < sizeof(ipv4_header_t) || ihl > l3_len) {
        log_malformed(102, ip->ver_ihl);
        return;
    }

    u16 total = ip_htons(ip->total_len);
    if (total < ihl || total > l3_len) {
        log_malformed(103, total);
        return;
    }
    if (net_checksum16(l3, ihl) != 0u) {
        log_malformed(105, ip->checksum);
        return;
    }

    const u8* l4 = l3 + ihl;
    u16 l4_len = (u16)(total - ihl);
    u32 src_ip = ip_htonl(ip->src_ip);
    arp_touch(src_ip, eth->src);

    /* ─── NAT hook: DNAT / SNAT / port forwarding ─── */
    {
        u32 dst_ip_n = ip_htonl(ip->dst_ip);
        u16 nat_sport = 0u, nat_dport = 0u;
        int nat_rc = 1;
        if (ip->proto == IP_PROTO_TCP && l4_len >= sizeof(tcp_header_t)) {
            const tcp_header_t* tcp = (const tcp_header_t*)l4;
            nat_sport = ip_htons(tcp->src_port);
            nat_dport = ip_htons(tcp->dst_port);
            nat_rc = nat_process_inbound(src_ip, dst_ip_n, ip->proto,
                        nat_sport, nat_dport, (u8*)l3, total, (u8*)frame, len);
        } else if (ip->proto == IP_PROTO_UDP && l4_len >= sizeof(udp_header_t)) {
            const udp_header_t* udp = (const udp_header_t*)l4;
            nat_sport = ip_htons(udp->src_port);
            nat_dport = ip_htons(udp->dst_port);
            nat_rc = nat_process_inbound(src_ip, dst_ip_n, ip->proto,
                        nat_sport, nat_dport, (u8*)l3, total, (u8*)frame, len);
        } else if (ip->proto == IP_PROTO_ICMP) {
            nat_rc = nat_process_inbound(src_ip, dst_ip_n, ip->proto,
                        0u, 0u, (u8*)l3, total, (u8*)frame, len);
        }
        if (nat_rc == 0) return;  /* NAT consumed the packet (forwarded) */
    }

    if (ip->proto == IP_PROTO_TCP) {
        if (l4_len < sizeof(tcp_header_t)) {
            log_malformed(104, l4_len);
            return;
        }
        const tcp_header_t* tcp = (const tcp_header_t*)l4;
        u16 src_port = ip_htons(tcp->src_port);
        u16 dst_port = ip_htons(tcp->dst_port);
        u16 tcp_hlen = (u16)(((u16)(tcp->data_offset >> 4) & 0x0Fu) * 4u);
        if (tcp_hlen < sizeof(tcp_header_t) || tcp_hlen > l4_len) {
            log_malformed(107, tcp->data_offset);
            return;
        }
        if (net_tcp_checksum(src_ip, g_st.local_ip, l4, l4_len) != 0u) {
            log_malformed(108, tcp->checksum);
            return;
        }
        {
            int sk = socket_match(IP_PROTO_TCP, dst_port, src_ip, src_port);
            if (sk < 0 && (tcp->flags & TCP_FLAG_SYN) && !(tcp->flags & TCP_FLAG_ACK)) {
                int listener = socket_listener(IP_PROTO_TCP, dst_port);
                if (listener >= 0) {
                    for (u32 ci = 0u; ci < SOCK_MAX; ci++) {
                        if (g_sock[ci].valid) continue;
                        g_sock[ci] = g_sock[(u32)listener];
                        g_sock[ci].id = g_sock_next++;
                        g_sock[ci].remote_ip = src_ip;
                        g_sock[ci].remote_port = src_port;
                        g_sock[ci].state = NET_SOCKET_SYN_RECEIVED;
                        g_sock[ci].parent_id = g_sock[(u32)listener].id;
                        g_sock[ci].tx_seq = tcp_initial_sequence(g_sock[ci].id);
                        g_sock[ci].rx_seq = ip_htonl(tcp->seq) + 1u;
                        g_sock_rx_head[ci] = 0u; g_sock_rx_tail[ci] = 0u; g_sock_rx_count[ci] = 0u;
                        g_st.sockets_open++;
                        (void)tcp_raw_send_syn_ack(g_st.local_ip, src_ip, dst_port, src_port,
                                                   g_sock[ci].tx_seq, g_sock[ci].rx_seq);
                        sk = (int)ci;
                        break;
                    }
                }
            }
            if (sk >= 0) {
                net_socket_info_t* socket = &g_sock[(u32)sk];
                u32 seq = ip_htonl(tcp->seq);
                u32 ack = ip_htonl(tcp->ack);
                u16 payload_len = (u16)(l4_len - tcp_hlen);
                if (tcp->flags & TCP_FLAG_RST) {
                    socket_abort_slot((u32)sk);
                    } else {
                    if ((tcp->flags & TCP_FLAG_ACK) && socket->tx_pending &&
                        ack >= socket->pending_seq + socket->pending_len) {
                        u32 sample = timer_ticks() - socket->tx_timestamp;
                        socket->tx_pending = 0u;
                        socket->pending_len = 0u;
                        tcp_on_ack(socket, timer_ticks(), sample);
                    } else if ((tcp->flags & TCP_FLAG_ACK) && socket->tx_pending &&
                               ack == socket->pending_seq) {
                        socket->dup_acks++;
                    }
                    if ((tcp->flags & TCP_FLAG_ACK) && socket->state == NET_SOCKET_SYN_RECEIVED) {
                        socket->state = NET_SOCKET_ESTABLISHED;
                    }
                    if ((tcp->flags & TCP_FLAG_SYN) && (tcp->flags & TCP_FLAG_ACK) &&
                        socket->state == NET_SOCKET_SYN_SENT) {
                        socket->tx_seq += 1u;
                        socket->rx_seq = seq + 1u;
                        socket->state = NET_SOCKET_ESTABLISHED;
                        (void)tcp_raw_send_ack(g_st.local_ip, src_ip, dst_port, src_port,
                                               socket->tx_seq, socket->rx_seq);
                    }
                    if (payload_len > 0u && socket->state == NET_SOCKET_ESTABLISHED) {
                        if (seq == socket->rx_seq) {
                            socket_queue_packet(sk, src_ip, src_port, l4 + tcp_hlen, payload_len);
                            socket->rx_seq += payload_len;
                            (void)tcp_raw_send_ack(g_st.local_ip, src_ip, dst_port, src_port,
                                                   socket->tx_seq, socket->rx_seq);
                        }
                    }
                    if ((tcp->flags & TCP_FLAG_FIN) && socket->state != 0u) {
                        socket->rx_seq = seq + payload_len + 1u;
                        socket->state = NET_SOCKET_CLOSING;
                        (void)tcp_raw_send_ack(g_st.local_ip, src_ip, dst_port, src_port,
                                               socket->tx_seq, socket->rx_seq);
                    }
                }
            }
        }
        lastmap_on_tcp(src_ip, src_port, dst_port, tcp->flags);
        return;
    }

    if (ip->proto == IP_PROTO_ICMP) {
        if (l4_len < sizeof(icmp_echo_header_t)) return;
        const icmp_echo_header_t* icmp = (const icmp_echo_header_t*)l4;
        if (icmp->type == 11u && icmp->code == 0u && l4_len >= 8u + sizeof(ipv4_header_t) + sizeof(icmp_echo_header_t)) {
            const ipv4_header_t* original_ip = (const ipv4_header_t*)(l4 + 8u);
            u16 original_ihl = (u16)((original_ip->ver_ihl & 0x0Fu) * 4u);
            if ((original_ip->ver_ihl >> 4) == 4u &&
                original_ihl >= sizeof(ipv4_header_t) && original_ihl + sizeof(icmp_echo_header_t) <= l4_len - 8u) {
                const icmp_echo_header_t* original_icmp = (const icmp_echo_header_t*)((const u8*)original_ip + original_ihl);
                if (original_ip->proto == IP_PROTO_ICMP &&
                    ip_htons(original_icmp->identifier) == 0x4C54u &&
                    ip_htons(original_icmp->sequence) == g_trace_sequence) {
                    g_trace_received = 1u;
                    g_trace_reached = 0u;
                    g_trace_hop_ip = src_ip;
                    g_trace_ticks = timer_ticks();
                }
            }
            return;
        }
        if (icmp->type == 0u && icmp->code == 0u &&
            g_ping_ip == src_ip && ip_htons(icmp->sequence) == g_ping_sequence &&
            ip_htons(icmp->identifier) == (u16)(0x4C53u)) {
            g_ping_received = 1u;
            g_ping_ticks = timer_ticks();
            g_ping_ttl = ip->ttl;
        }
        if (icmp->type == 0u && icmp->code == 0u &&
            ip_htons(icmp->identifier) == 0x4C54u && ip_htons(icmp->sequence) == g_trace_sequence) {
            g_trace_received = 1u;
            g_trace_reached = 1u;
            g_trace_hop_ip = src_ip;
            g_trace_ticks = timer_ticks();
        }
        return;
    }

    if (ip->proto == IP_PROTO_UDP) {
        if (l4_len < sizeof(udp_header_t)) {
            log_malformed(105, l4_len);
            return;
        }
        const udp_header_t* udp = (const udp_header_t*)l4;
        u16 udplen = ip_htons(udp->len);
        if (udplen < sizeof(udp_header_t) || udplen > l4_len) {
            log_malformed(106, udplen);
            return;
        }
        /* IPv4 permits an all-zero UDP checksum, but a supplied checksum
           must still validate before the datagram reaches a socket. */
        if (udp->checksum != 0u && net_udp_checksum(src_ip, g_st.local_ip, l4, udplen) != 0u) {
            log_malformed(109, udp->checksum);
            return;
        }
        u16 src_port = ip_htons(udp->src_port);
        u16 dst_port = ip_htons(udp->dst_port);
        const u8* payload = l4 + sizeof(udp_header_t);
        u16 payload_len = (u16)(udplen - sizeof(udp_header_t));
        if (wireguard_on_udp(src_ip, src_port, dst_port, payload, payload_len)) {
            return;
        }
        int sk = socket_match(IP_PROTO_UDP, dst_port, src_ip, src_port);
        if (sk >= 0 && payload_len > 0u) {
            g_sock[(u32)sk].state = 2u;
            socket_queue_packet(sk, src_ip, src_port, payload, payload_len);
        }
        lastmap_on_udp(src_ip, src_port, dst_port, payload, payload_len);
    }
}

int netstack_ping(u32 remote_ip, u16 sequence, u32 timeout_ticks, u32* out_ticks, u8* out_ttl) {
    u8 frame[sizeof(eth_header_t) + sizeof(ipv4_header_t) + sizeof(icmp_echo_header_t) + 32u];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    icmp_echo_header_t* icmp = (icmp_echo_header_t*)((u8*)ip + sizeof(ipv4_header_t));
    u8* payload = (u8*)icmp + sizeof(icmp_echo_header_t);
    if (netstack_prepare_peer(remote_ip, timeout_ticks) != 0) return -2;
    ethernet_default_mac(eth->src, eth->dst);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);
    if (ip_build_ipv4(ip, g_st.local_ip, remote_ip, IP_PROTO_ICMP,
                      (u16)(sizeof(icmp_echo_header_t) + 32u), sequence) != 0) return -1;
    icmp->type = 8u;
    icmp->code = 0u;
    icmp->checksum = 0u;
    icmp->identifier = ip_htons(0x4C53u);
    icmp->sequence = ip_htons(sequence);
    for (u32 i = 0u; i < 32u; i++) payload[i] = (u8)i;
    icmp->checksum = ip_htons(net_checksum16((const u8*)icmp,
                                              sizeof(icmp_echo_header_t) + 32u));

    g_ping_ip = remote_ip;
    g_ping_sequence = sequence;
    g_ping_received = 0u;
    u32 start = timer_ticks();
    if (ethernet_send_frame(frame, sizeof(frame)) != 0) return -1;
    u32 deadline = start + (timeout_ticks ? timeout_ticks : 100u);
    u32 guard = (timeout_ticks ? timeout_ticks : 100u) * 10000u;
    while (!g_ping_received && guard-- != 0u && (i32)(timer_ticks() - deadline) < 0) (void)nic_poll(8u);
    if (!g_ping_received) return -3;
    if (out_ticks) *out_ticks = g_ping_ticks - start;
    if (out_ttl) *out_ttl = g_ping_ttl;
    return 0;
}

int netstack_traceroute_probe(u32 remote_ip, u8 hop_limit, u16 sequence, u32 timeout_ticks,
                              u32* out_hop_ip, u32* out_ticks, u8* out_reached) {
    u8 frame[sizeof(eth_header_t) + sizeof(ipv4_header_t) + sizeof(icmp_echo_header_t)];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv4_header_t* ip = (ipv4_header_t*)(frame + sizeof(eth_header_t));
    icmp_echo_header_t* icmp = (icmp_echo_header_t*)((u8*)ip + sizeof(ipv4_header_t));
    u32 start, deadline, guard;
    if (hop_limit == 0u || netstack_prepare_peer(remote_ip, timeout_ticks) != 0) return -1;
    ethernet_default_mac(eth->src, eth->dst); eth->ethertype = ip_htons(ETH_TYPE_IPV4);
    if (ip_build_ipv4(ip, g_st.local_ip, remote_ip, IP_PROTO_ICMP, sizeof(*icmp), sequence) != 0) return -1;
    ip->ttl = hop_limit;
    ip->checksum = 0u;
    ip->checksum = ip_htons(net_checksum16((const u8*)ip, sizeof(*ip)));
    icmp->type = 8u; icmp->code = 0u; icmp->checksum = 0u;
    icmp->identifier = ip_htons(0x4C54u); icmp->sequence = ip_htons(sequence);
    icmp->checksum = ip_htons(net_checksum16((const u8*)icmp, sizeof(*icmp)));
    g_trace_received = 0u; g_trace_reached = 0u; g_trace_hop_ip = 0u; g_trace_sequence = sequence;
    start = timer_ticks();
    if (ethernet_send_frame(frame, sizeof(frame)) != 0) return -1;
    deadline = start + (timeout_ticks ? timeout_ticks : 100u);
    guard = (timeout_ticks ? timeout_ticks : 100u) * 10000u;
    while (!g_trace_received && guard-- != 0u && (i32)(timer_ticks() - deadline) < 0) (void)nic_poll(8u);
    if (!g_trace_received) return -2;
    if (out_hop_ip) *out_hop_ip = g_trace_hop_ip;
    if (out_ticks) *out_ticks = g_trace_ticks - start;
    if (out_reached) *out_reached = g_trace_reached;
    return 0;
}

int netstack_ipv6_local(u8 out_addr[16]) {
    if (!out_addr || g_ipv6_local[0] == 0u) return -1;
    memcpy(out_addr, g_ipv6_local, 16u);
    return 0;
}

int netstack_ipv6_ping(const u8 remote[16], u16 sequence, u32 timeout_ticks, u32* out_ticks) {
    int idx;
    u32 start;
    u32 deadline;
    u8 frame[1514];
    eth_header_t* eth = (eth_header_t*)frame;
    ipv6_header_t* ip = (ipv6_header_t*)(frame + sizeof(eth_header_t));
    u8* body = frame + sizeof(eth_header_t) + sizeof(ipv6_header_t);
    u8 mac[6];
    u8 src_mac[6];
    if (!remote || g_ipv6_local[0] == 0u || nic_get_mac(src_mac) != 0) return -1;
    idx = ndp_find(remote);
    if (idx < 0 || (timer_ticks() - g_ndp[(u32)idx].last_seen_tick) > NDP_ENTRY_TTL) {
        if (ipv6_send_neighbor_solicitation(remote) != 0) return -2;
        deadline = timer_ticks() + (timeout_ticks ? timeout_ticks : 100u);
        do {
            (void)nic_poll(8u);
            idx = ndp_find(remote);
            if (idx >= 0) break;
        } while ((i32)(timer_ticks() - deadline) < 0);
    }
    if (idx < 0) return -3;
    memcpy(mac, g_ndp[(u32)idx].mac, 6u);
    if (ipv6_build_header(ip, g_ipv6_local, remote, IPV6_NEXT_ICMP, 40u, 64u) != 0) return -1;
    body[0] = ICMPV6_ECHO_REQUEST; body[1] = 0u; body[2] = 0u; body[3] = 0u;
    *(u16*)(body + 4u) = ip_htons(0x4C53u); *(u16*)(body + 6u) = ip_htons(sequence);
    for (u32 i = 0u; i < 32u; i++) body[8u + i] = (u8)i;
    ((icmpv6_header_t*)body)->checksum = ipv6_transport_checksum(ip->src, ip->dst, IPV6_NEXT_ICMP, body, 40u);
    for (u32 i = 0u; i < 6u; i++) { eth->src[i] = src_mac[i]; eth->dst[i] = mac[i]; }
    eth->ethertype = ip_htons(ETH_TYPE_IPV6);
    g_ipv6_ping_received = 0u; memcpy(g_ipv6_ping_peer, remote, 16u); g_ipv6_ping_sequence = sequence;
    start = timer_ticks();
    if (ethernet_send_frame(frame, (u16)(sizeof(eth_header_t) + sizeof(ipv6_header_t) + 40u)) != 0) return -1;
    deadline = start + (timeout_ticks ? timeout_ticks : 100u);
    while (!g_ipv6_ping_received && (i32)(timer_ticks() - deadline) < 0) (void)nic_poll(8u);
    if (!g_ipv6_ping_received) return -4;
    if (out_ticks) *out_ticks = g_ipv6_ping_ticks - start;
    return 0;
}

void netstack_set_local(u32 ip, u32 mask, u32 gateway) {
    g_st.local_ip = ip;
    g_st.local_mask = mask;
    g_st.local_gw = gateway;
    if (g_route[0].valid) {
        g_route[0].network = ip & mask;
        g_route[0].mask = mask;
    }
    if (g_route[1].valid) g_route[1].gateway = gateway;
}

void netstack_status(netstack_status_t* out) {
    if (!out) return;
    *out = g_st;
}

u32 netstack_arp_count(void) {
    return g_st.arp_entries;
}

int netstack_arp_get(u32 index, net_arp_entry_t* out) {
    if (!out) return -1;
    u32 c = 0u;
    for (u32 i = 0; i < ARP_MAX; i++) {
        if (!g_arp[i].valid) continue;
        if (c == index) {
            *out = g_arp[i];
            return 0;
        }
        c++;
    }
    return -1;
}

int netstack_arp_resolve(u32 ip, u32 timeout_ticks) {
    int idx = arp_find_ip(ip);
    u32 now = timer_ticks();
    if (idx >= 0 && (now - g_arp[(u32)idx].last_seen_tick) <= ARP_ENTRY_TTL) {
        ethernet_set_peer_mac(g_arp[(u32)idx].mac);
        return 0;
    }
    if (ip == 0u || g_st.local_ip == 0u || arp_send_request(ip) != 0) return -1;
    u32 deadline = now + (timeout_ticks ? timeout_ticks : 100u);
    do {
        (void)nic_poll(8u);
        idx = arp_find_ip(ip);
        if (idx >= 0) {
            ethernet_set_peer_mac(g_arp[(u32)idx].mac);
            return 0;
        }
    } while ((i32)(timer_ticks() - deadline) < 0);
    return -1;
}

int netstack_prepare_peer(u32 remote_ip, u32 timeout_ticks) {
    u32 target = remote_ip;
    for (u32 i = 0u; i < ROUTE_MAX; i++) {
        if (route_match(remote_ip, &g_route[i])) {
            if (g_route[i].gateway != 0u) target = g_route[i].gateway;
            break;
        }
    }
    return netstack_arp_resolve(target, timeout_ticks);
}

u32 netstack_route_count(void) {
    return g_st.route_entries;
}

int netstack_route_get(u32 index, net_route_entry_t* out) {
    if (!out) return -1;
    u32 c = 0u;
    for (u32 i = 0; i < ROUTE_MAX; i++) {
        if (!g_route[i].valid) continue;
        if (c == index) {
            *out = g_route[i];
            return 0;
        }
        c++;
    }
    return -1;
}

int netstack_route_add(u32 network, u32 mask, u32 gateway, u32 metric) {
    for (u32 i = 0; i < ROUTE_MAX; i++) {
        if (!g_route[i].valid) {
            g_route[i].valid = 1u;
            g_route[i].network = network;
            g_route[i].mask = mask;
            g_route[i].gateway = gateway;
            g_route[i].metric = metric;
            g_st.route_entries++;
            return 0;
        }
    }
    return -1;
}

int netstack_socket_open(u32 proto, u16 local_port, u32 remote_ip, u16 remote_port) {
    for (u32 i = 0; i < SOCK_MAX; i++) {
        if (!g_sock[i].valid) {
            g_sock[i].valid = 1u;
            g_sock[i].id = g_sock_next++;
            g_sock[i].proto = proto;
            g_sock[i].local_port = local_port;
            g_sock[i].remote_ip = remote_ip;
            g_sock[i].remote_port = remote_port;
            g_sock[i].state = 1u;
            g_sock[i].rx_pending = 0u;
            g_sock[i].rx_dropped = 0u;
            g_sock[i].tx_seq = 0u;
            g_sock[i].rx_seq = 0u;
            g_sock[i].pending_seq = 0u;
            g_sock[i].pending_len = 0u;
            g_sock[i].tx_retries = 0u;
            g_sock[i].tx_pending = 0u;
            g_sock[i].dup_acks = 0u;
            g_sock[i].rto_ticks = TCP_INITIAL_RTO;
            g_sock[i].srtt_ticks = 0u;
            g_sock[i].rttvar_ticks = 0u;
            g_sock[i].cwnd_bytes = TCP_MSS;
            g_sock[i].ssthresh_bytes = 8192u;
            g_sock[i].tx_timestamp = 0u;
            g_sock[i].ipv6 = 0u;
            g_sock[i].parent_id = 0u;
            g_sock[i].owner_task = 0u;
            memset(g_sock[i].remote_ipv6, 0, sizeof(g_sock[i].remote_ipv6));
            g_sock_rx_head[i] = 0u;
            g_sock_rx_tail[i] = 0u;
            g_sock_rx_count[i] = 0u;
            g_st.sockets_open++;
            return (int)g_sock[i].id;
        }
    }
    return -1;
}

int netstack_socket_listen(u16 local_port) {
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (g_sock[i].valid) continue;
        memset(&g_sock[i], 0, sizeof(g_sock[i]));
        g_sock[i].valid = 1u;
        g_sock[i].id = g_sock_next++;
        g_sock[i].proto = IP_PROTO_TCP;
        g_sock[i].local_port = local_port;
        g_sock[i].state = NET_SOCKET_LISTEN;
        g_st.sockets_open++;
        return (int)g_sock[i].id;
    }
    return -1;
}

int netstack_socket_accept(u32 listener_id, u32* out_client_id, u32* out_remote_ip, u16* out_remote_port) {
    int listener = socket_slot_by_id(listener_id);
    if (listener < 0 || g_sock[(u32)listener].state != NET_SOCKET_LISTEN || !out_client_id) return -1;
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (!g_sock[i].valid || g_sock[i].parent_id != listener_id || g_sock[i].state != NET_SOCKET_ESTABLISHED) continue;
        *out_client_id = g_sock[i].id;
        if (out_remote_ip) *out_remote_ip = g_sock[i].remote_ip;
        if (out_remote_port) *out_remote_port = g_sock[i].remote_port;
        g_sock[i].parent_id = 0u;
        return 0;
    }
    return -1;
}

int netstack_socket_open6(u32 proto, u16 local_port, const u8 remote_ip[16], u16 remote_port) {
    if (!remote_ip || (proto != IP_PROTO_TCP && proto != IP_PROTO_UDP)) return -1;
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (!g_sock[i].valid) {
            memset(&g_sock[i], 0, sizeof(g_sock[i]));
            g_sock[i].valid = 1u; g_sock[i].id = g_sock_next++; g_sock[i].proto = proto;
            g_sock[i].local_port = local_port; g_sock[i].remote_port = remote_port; g_sock[i].state = 1u; g_sock[i].ipv6 = 1u;
            g_sock[i].rto_ticks = TCP_INITIAL_RTO;
            g_sock[i].cwnd_bytes = TCP_MSS;
            g_sock[i].ssthresh_bytes = 8192u;
            g_sock[i].owner_task = 0u;
            memcpy(g_sock[i].remote_ipv6, remote_ip, 16u);
            g_sock_rx_head[i] = 0u; g_sock_rx_tail[i] = 0u; g_sock_rx_count[i] = 0u;
            g_st.sockets_open++;
            return (int)g_sock[i].id;
        }
    }
    return -1;
}

int netstack_socket_close(u32 sock_id) {
    int s = socket_slot_by_id(sock_id);
    if (s < 0) return -1;
    g_sock[(u32)s].valid = 0u;
    g_sock_rx_head[(u32)s] = 0u;
    g_sock_rx_tail[(u32)s] = 0u;
    g_sock_rx_count[(u32)s] = 0u;
    if (g_st.sockets_open > 0u) g_st.sockets_open--;
    return 0;
}

int netstack_socket_set_owner(u32 sock_id, u32 owner_task) {
    int s = socket_slot_by_id(sock_id);
    if (s < 0) return -1;
    g_sock[(u32)s].owner_task = owner_task;
    return 0;
}

u32 netstack_socket_owner(u32 sock_id) {
    int s = socket_slot_by_id(sock_id);
    return s < 0 ? 0u : g_sock[(u32)s].owner_task;
}

void netstack_socket_release_task(u32 task_id) {
    if (task_id == 0u) return;
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        if (g_sock[i].valid && g_sock[i].owner_task == task_id) {
            socket_abort_slot(i);
        }
    }
}

int netstack_socket_recv(u32 sock_id, u8* out, u16 out_len, u32* out_src_ip, u16* out_src_port) {
    int s = socket_slot_by_id(sock_id);
    if (s < 0 || !out || out_len == 0u || g_sock_rx_count[(u32)s] == 0u) return -1;
    u32 q = g_sock_rx_head[(u32)s];
    socket_rx_packet_t* p = &g_sock_rx[(u32)s][q];
    u16 n = p->len < out_len ? p->len : out_len;
    memcpy(out, p->data, n);
    if (out_src_ip) *out_src_ip = p->src_ip;
    if (out_src_port) *out_src_port = p->src_port;
    g_sock_rx_head[(u32)s] = (u8)((q + 1u) % SOCK_RX_CAP);
    g_sock_rx_count[(u32)s]--;
    g_sock[(u32)s].rx_pending = g_sock_rx_count[(u32)s];
    return (int)n;
}

int netstack_socket_recv6(u32 sock_id, u8* out, u16 out_len, u8 out_src_ipv6[16], u16* out_src_port) {
    int s = socket_slot_by_id(sock_id);
    if (s < 0 || !out || out_len == 0u || g_sock_rx_count[(u32)s] == 0u) return -1;
    u32 q = g_sock_rx_head[(u32)s];
    socket_rx_packet_t* p = &g_sock_rx[(u32)s][q];
    u16 n = p->len < out_len ? p->len : out_len;
    memcpy(out, p->data, n);
    if (out_src_ipv6) memcpy(out_src_ipv6, p->src_ipv6, 16u);
    if (out_src_port) *out_src_port = p->src_port;
    g_sock_rx_head[(u32)s] = (u8)((q + 1u) % SOCK_RX_CAP);
    g_sock_rx_count[(u32)s]--;
    g_sock[(u32)s].rx_pending = g_sock_rx_count[(u32)s];
    return (int)n;
}

int netstack_socket_send(u32 sock_id, const u8* data, u16 len) {
    int s = socket_slot_by_id(sock_id);
    if (s < 0) return -1;
    net_socket_info_t* sk = &g_sock[(u32)s];

    if (sk->ipv6) {
        u8 dst_mac[6];
        if (ndp_resolve(sk->remote_ipv6, 100u, dst_mac) != 0) return -5;
        if (sk->proto == IP_PROTO_UDP) {
            return ipv6_udp_send(g_ipv6_local, sk->remote_ipv6, dst_mac,
                                 sk->local_port, sk->remote_port, data, len);
        }
        if (sk->proto == IP_PROTO_TCP) {
            if (len != 0u || data != 0) {
                if (sk->state != NET_SOCKET_ESTABLISHED) return -4;
                int rc = ipv6_tcp_send(g_ipv6_local, sk->remote_ipv6, dst_mac,
                                       sk->local_port, sk->remote_port, sk->tx_seq,
                                       sk->rx_seq, TCP_FLAG_ACK | TCP_FLAG_PSH, data, len);
                if (rc == 0) {
                    if (len > sizeof(g_sock_tx[(u32)s])) return -6;
                    memcpy(g_sock_tx[(u32)s], data, len);
                    sk->pending_seq = sk->tx_seq; sk->pending_len = len;
                    sk->tx_retries = 0u; sk->dup_acks = 0u; sk->tx_pending = 1u;
                    sk->tx_timestamp = timer_ticks();
                    g_sock_tx_tick[(u32)s] = sk->tx_timestamp;
                    sk->tx_seq += len;
                }
                return rc;
            }
            sk->tx_seq = tcp_initial_sequence(sk->id); sk->state = NET_SOCKET_SYN_SENT;
            sk->tx_retries = 0u; g_sock_tx_tick[(u32)s] = timer_ticks();
            return ipv6_tcp_send(g_ipv6_local, sk->remote_ipv6, dst_mac, sk->local_port,
                                 sk->remote_port, sk->tx_seq, 0u, TCP_FLAG_SYN, 0, 0u);
        }
        return -3;
    }

    int route_ok = 0;
    for (u32 i = 0; i < ROUTE_MAX; i++) {
        if (route_match(sk->remote_ip, &g_route[i])) {
            route_ok = 1;
            break;
        }
    }
    if (!route_ok) return -2;
    if (netstack_prepare_peer(sk->remote_ip, 100u) != 0) return -5;

    if (sk->proto == IP_PROTO_UDP) {
        return udp_raw_send(g_st.local_ip, sk->remote_ip, sk->local_port, sk->remote_port, data, len);
    }
    if (sk->proto == IP_PROTO_TCP) {
        if (len != 0u || data != 0) {
            if (sk->state != NET_SOCKET_ESTABLISHED) return -4;
            int rc = tcp_raw_send_data(g_st.local_ip, sk->remote_ip, sk->local_port,
                                       sk->remote_port, sk->tx_seq, sk->rx_seq, data, len);
            if (rc == 0) {
                if (len > sizeof(g_sock_tx[(u32)s])) return -6;
                memcpy(g_sock_tx[(u32)s], data, len);
                sk->pending_seq = sk->tx_seq;
                sk->pending_len = len;
                sk->tx_retries = 0u;
                sk->dup_acks = 0u;
                sk->tx_pending = 1u;
                sk->tx_timestamp = timer_ticks();
                g_sock_tx_tick[(u32)s] = sk->tx_timestamp;
                sk->tx_seq += len;
            }
            return rc;
        }
        sk->tx_seq = tcp_initial_sequence(sk->id);
        sk->state = NET_SOCKET_SYN_SENT;
        sk->tx_pending = 0u;
        sk->tx_retries = 0u;
        sk->dup_acks = 0u;
        sk->rto_ticks = TCP_INITIAL_RTO;
        sk->tx_timestamp = timer_ticks();
        g_sock_tx_tick[(u32)s] = sk->tx_timestamp;
        return tcp_raw_send_syn(g_st.local_ip, sk->remote_ip, sk->local_port, sk->remote_port, sk->tx_seq);
    }
    return -3;
}

u32 netstack_socket_count(void) {
    return g_st.sockets_open;
}

int netstack_socket_get(u32 index, net_socket_info_t* out) {
    if (!out) return -1;
    u32 c = 0u;
    for (u32 i = 0; i < SOCK_MAX; i++) {
        if (!g_sock[i].valid) continue;
        if (c == index) {
            *out = g_sock[i];
            return 0;
        }
        c++;
    }
    return -1;
}

int netstack_socket_state(u32 sock_id, u8* out_state) {
    int s = socket_slot_by_id(sock_id);
    if (s < 0 || !out_state) return -1;
    *out_state = g_sock[(u32)s].state;
    return 0;
}

u32 netstack_local_ip(void) { return g_st.local_ip; }
u32 netstack_local_mask(void) { return g_st.local_mask; }
u32 netstack_local_gw(void) { return g_st.local_gw; }

void netstack_tick(void) {
    u32 now = timer_ticks();
    for (u32 i = 0u; i < SOCK_MAX; i++) {
        net_socket_info_t* sk = &g_sock[i];
        if (!sk->valid || sk->proto != IP_PROTO_TCP) continue;
        u32 rto = sk->rto_ticks ? sk->rto_ticks : TCP_INITIAL_RTO;
        if (sk->state == NET_SOCKET_SYN_SENT && (now - g_sock_tx_tick[i]) >= rto) {
            if (sk->tx_retries >= TCP_MAX_RETRIES) {
                socket_abort_slot(i);
                continue;
            }
            u8 mac[6];
            int sent = -1;
            if (sk->ipv6) {
                if (ndp_resolve(sk->remote_ipv6, 0u, mac) == 0)
                    sent = ipv6_tcp_send(g_ipv6_local, sk->remote_ipv6, mac, sk->local_port,
                                         sk->remote_port, sk->tx_seq, 0u, TCP_FLAG_SYN, 0, 0u);
            } else if (netstack_prepare_peer(sk->remote_ip, 0u) == 0) {
                sent = tcp_raw_send_syn(g_st.local_ip, sk->remote_ip, sk->local_port, sk->remote_port, sk->tx_seq);
            }
            if (sent == 0) {
                sk->tx_retries++;
                sk->rto_ticks = tcp_clamp_rto(rto * 2u);
                g_sock_tx_tick[i] = now;
            }
        } else if (sk->state == NET_SOCKET_ESTABLISHED && sk->tx_pending &&
                   (sk->dup_acks >= 3u || (now - g_sock_tx_tick[i]) >= rto)) {
            if (sk->tx_retries >= TCP_MAX_RETRIES) {
                socket_abort_slot(i);
                continue;
            }
            u8 mac[6];
            int sent = -1;
            if (sk->ipv6) {
                if (ndp_resolve(sk->remote_ipv6, 0u, mac) == 0)
                    sent = ipv6_tcp_send(g_ipv6_local, sk->remote_ipv6, mac, sk->local_port,
                                         sk->remote_port, sk->pending_seq, sk->rx_seq,
                                         TCP_FLAG_ACK | TCP_FLAG_PSH, g_sock_tx[i], sk->pending_len);
            } else if (netstack_prepare_peer(sk->remote_ip, 0u) == 0) {
                sent = tcp_raw_send_data(g_st.local_ip, sk->remote_ip, sk->local_port, sk->remote_port,
                                         sk->pending_seq, sk->rx_seq, g_sock_tx[i], sk->pending_len);
            }
            if (sent == 0) {
                sk->tx_retries++;
                if (sk->dup_acks >= 3u) sk->dup_acks = 0u;
                sk->ssthresh_bytes = sk->cwnd_bytes / 2u;
                if (sk->ssthresh_bytes < 2u * TCP_MSS) sk->ssthresh_bytes = 2u * TCP_MSS;
                sk->cwnd_bytes = TCP_MSS;
                sk->rto_ticks = tcp_clamp_rto(rto * 2u);
                g_sock_tx_tick[i] = now;
            }
        }
    }
}
