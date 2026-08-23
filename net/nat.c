#include "nat.h"
#include "ethernet.h"
#include "ip.h"
#include "tcp_raw.h"
#include "udp_raw.h"
#include "checksum.h"
#include "netstack.h"
#include "nic.h"
#include "timer.h"
#include "string.h"
#include "entropy.h"

/* ─── Static data ────────────────────────────────────────────────────────── */

static conntrack_entry_t g_ct[CONNTRACK_MAX];
static nat_rule_t        g_rules[NAT_RULE_MAX];
static nat_status_t      g_nat_st;
static u8                g_nat_initialized = 0u;

/* TCP conntrack timeouts (ticks — 1 tick ~= 10 ms) */
#define CT_TCP_NEW_TIMEOUT     300u   /* 3 s */
#define CT_TCP_EST_TIMEOUT    3000u   /* 30 s */
#define CT_TCP_CLOSING_TIMEOUT 100u   /* 1 s */
#define CT_UDP_TIMEOUT         500u   /* 5 s */
#define CT_ICMP_TIMEOUT        100u   /* 1 s */

#define CT_EPHEMERAL_START 32768u
#define CT_EPHEMERAL_END   61000u

/* ─── helpers ────────────────────────────────────────────────────────────── */

static int ct_key_eq(const conntrack_key_t* a, const conntrack_key_t* b) {
    return a->src_ip  == b->src_ip  && a->dst_ip  == b->dst_ip &&
           a->src_port == b->src_port && a->dst_port == b->dst_port &&
           a->proto    == b->proto;
}

static int ct_key_rev_eq(const conntrack_key_t* a, const conntrack_key_t* b) {
    /* Check if b is the reverse of a (reply direction) */
    return a->src_ip  == b->dst_ip  && a->dst_ip  == b->src_ip &&
           a->src_port == b->dst_port && a->dst_port == b->src_port &&
           a->proto    == b->proto;
}

/* Find conntrack by exact 5-tuple */
static int ct_find(const conntrack_key_t* key) {
    for (u32 i = 0u; i < CONNTRACK_MAX; i++) {
        if (g_ct[i].valid && ct_key_eq(&g_ct[i].key, key)) return (int)i;
    }
    return -1;
}

/* Find conntrack by reverse 5-tuple (reply direction) */
static int ct_find_rev(const conntrack_key_t* key) {
    for (u32 i = 0u; i < CONNTRACK_MAX; i++) {
        if (g_ct[i].valid && ct_key_rev_eq(&g_ct[i].key, key)) return (int)i;
    }
    return -1;
}

/* Allocate a new conntrack slot (returns oldest if full) */
static int ct_alloc(void) {
    u32 oldest = 0u;
    u32 oldest_ticks = 0xFFFFFFFFu;
    for (u32 i = 0u; i < CONNTRACK_MAX; i++) {
        if (!g_ct[i].valid) return (int)i;
        if (g_ct[i].timeout_tick < oldest_ticks) {
            oldest_ticks = g_ct[i].timeout_tick;
            oldest = i;
        }
    }
    /* Evict the oldest entry */
    g_ct[oldest].valid = 0u;
    return (int)oldest;
}

/* Allocate an ephemeral port for SNAT.
 *
 * The walk is sequential, but the starting point is randomized in nat_init().
 * A strictly predictable sequence from a fixed base would let an off-path
 * attacker who knows how many connections the box has made guess the external
 * port of a NAT mapping and inject traffic into it. */
static u16 ct_ephemeral(void) {
    u32 port = g_nat_st.ephemeral_next;
    if (port < CT_EPHEMERAL_START || port >= CT_EPHEMERAL_END) port = CT_EPHEMERAL_START;
    g_nat_st.ephemeral_next = port + 1u;
    return (u16)port;
}

/* Recompute IPv4 header checksum */
static void ip_fix_checksum(ipv4_header_t* ip) {
    ip->checksum = 0u;
    ip->checksum = ip_htons(net_checksum16((const u8*)ip, sizeof(ipv4_header_t)));
}

/* Recompute TCP checksum after address/port changes */
static void tcp_fix_checksum(u32 new_src, u32 new_dst, tcp_header_t* tcp, u16 tcp_len) {
    tcp->checksum = 0u;
    tcp->checksum = ip_htons(net_tcp_checksum(new_src, new_dst, (const u8*)tcp, tcp_len));
}

/* Recompute UDP checksum after address/port changes */
static void udp_fix_checksum(u32 new_src, u32 new_dst, udp_header_t* udp, u16 udp_len) {
    udp->checksum = 0u;
    udp->checksum = ip_htons(net_udp_checksum(new_src, new_dst, (const u8*)udp, udp_len));
}

/* Resolve MAC and forward a pre-built frame (L2 + L3 payload already set).
   Returns 0 on success. */
static int nat_forward_frame(u8* frame, u16 frame_len, u32 target_ip) {
    eth_header_t* eth = (eth_header_t*)frame;
    if (netstack_arp_resolve(target_ip, 50u) != 0) return -1;
    ethernet_default_mac(eth->src, eth->dst);
    return ethernet_send_frame(frame, frame_len);
}

/* Build a complete L2+L3+L4 frame for forwarding a NATted packet.
   l3 data (IP header + payload) is provided; this function wraps it in an
   Ethernet header and sends it via ARP resolution. */
static int nat_forward_ip(u8* l3_data, u16 l3_len, u32 dst_ip) {
    u8 frame[1514];
    eth_header_t* eth = (eth_header_t*)frame;
    u16 frame_len;
    if (l3_len < sizeof(ipv4_header_t) || l3_len > 1500u) return -1;
    frame_len = (u16)(sizeof(eth_header_t) + l3_len);
    eth->ethertype = ip_htons(ETH_TYPE_IPV4);
    if (netstack_arp_resolve(dst_ip, 50u) != 0) return -1;
    ethernet_default_mac(eth->src, eth->dst);
    memcpy(frame + sizeof(eth_header_t), l3_data, l3_len);
    return ethernet_send_frame(frame, frame_len);
}

/* ─── Public API ─────────────────────────────────────────────────────────── */

void nat_init(void) {
    for (u32 i = 0u; i < CONNTRACK_MAX; i++) g_ct[i].valid = 0u;
    for (u32 i = 0u; i < NAT_RULE_MAX; i++) g_rules[i].valid = 0u;
    g_nat_st.ip_forward = 0u;
    /* Start the ephemeral range at a random offset rather than at
     * CT_EPHEMERAL_START, so the external port of the Nth mapping is not
     * derivable from the connection count. */
    {
        u8 seed[2];
        u32 span = CT_EPHEMERAL_END - CT_EPHEMERAL_START;
        entropy_fill(seed, sizeof(seed));
        g_nat_st.ephemeral_next = CT_EPHEMERAL_START +
            ((((u32)seed[0] << 8u) | (u32)seed[1]) % span);
    }
    g_nat_st.conntrack_count = 0u;
    g_nat_st.rule_count = 0u;
    g_nat_st.snat_packets = 0u;
    g_nat_st.dnat_packets = 0u;
    g_nat_initialized = 1u;
}

int nat_rule_add(u8 type, u8 proto, u16 external_port,
                 u32 internal_ip, u16 internal_port,
                 u32 internal_net, u32 internal_mask) {
    for (u32 i = 0u; i < NAT_RULE_MAX; i++) {
        if (!g_rules[i].valid) {
            g_rules[i].valid = 1u;
            g_rules[i].type = type;
            g_rules[i].proto = proto;
            g_rules[i].external_port = external_port;
            g_rules[i].internal_ip = internal_ip;
            g_rules[i].internal_port = internal_port;
            g_rules[i].internal_net = internal_net;
            g_rules[i].internal_mask = internal_mask;
            g_nat_st.rule_count++;
            return 0;
        }
    }
    return -1;
}

int nat_rule_del(u32 index) {
    u32 c = 0u;
    for (u32 i = 0u; i < NAT_RULE_MAX; i++) {
        if (!g_rules[i].valid) continue;
        if (c == index) {
            g_rules[i].valid = 0u;
            g_nat_st.rule_count--;
            return 0;
        }
        c++;
    }
    return -1;
}

int nat_rule_get(u32 index, nat_rule_t* out) {
    if (!out) return -1;
    u32 c = 0u;
    for (u32 i = 0u; i < NAT_RULE_MAX; i++) {
        if (!g_rules[i].valid) continue;
        if (c == index) { *out = g_rules[i]; return 0; }
        c++;
    }
    return -1;
}

u32 nat_rule_count(void) {
    return g_nat_st.rule_count;
}

void nat_set_ip_forward(u8 on) {
    g_nat_st.ip_forward = on;
}

u8 nat_ip_forward(void) {
    return g_nat_st.ip_forward;
}

u32 nat_conntrack_count(void) {
    return g_nat_st.conntrack_count;
}

int nat_conntrack_get(u32 index, conntrack_entry_t* out) {
    if (!out) return -1;
    u32 c = 0u;
    for (u32 i = 0u; i < CONNTRACK_MAX; i++) {
        if (!g_ct[i].valid) continue;
        if (c == index) { *out = g_ct[i]; return 0; }
        c++;
    }
    return -1;
}

void nat_status(nat_status_t* out) {
    if (out) *out = g_nat_st;
}

/* ─── Main NAT processing ────────────────────────────────────────────────── */

int nat_process_inbound(u32 src_ip, u32 dst_ip, u8 proto,
                         u16 src_port, u16 dst_port,
                         u8* l3_buf, u16 l3_len,
                         u8* frame, u16 frame_len) {
    conntrack_key_t key;
    int ct_idx;
    u32 now;
    (void)frame_len;

    if (!g_nat_initialized) nat_init();
    now = timer_ticks();

    /* ─── Step 1: Check conntrack for existing NAT mapping ─── */

    key.src_ip  = src_ip;
    key.dst_ip  = dst_ip;
    key.src_port = src_port;
    key.dst_port = dst_port;
    key.proto    = proto;

    ct_idx = ct_find(&key);

    if (ct_idx < 0) {
        /* Check reverse — this might be a return packet from a NATted connection */
        ct_idx = ct_find_rev(&key);
        if (ct_idx >= 0) {
            /* ── Reverse SNAT: packet is reply to a SNATted connection ── */
            conntrack_entry_t* ct = &g_ct[(u32)ct_idx];
            u32 orig_src = ct->key.src_ip;
            u16 orig_src_port = ct->key.src_port;

            if (l3_len < sizeof(ipv4_header_t)) return -1;
            ipv4_header_t* ip = (ipv4_header_t*)l3_buf;
            u16 ihl = (u16)((ip->ver_ihl & 0x0Fu) * 4u);
            u8* l4 = l3_buf + ihl;
            u16 l4_len = (u16)(l3_len - ihl);

            /* Rewrite destination back to original source */
            ip->dst_ip = ip_htonl(orig_src);
            ip_fix_checksum(ip);

            if (proto == IP_PROTO_TCP && l4_len >= sizeof(tcp_header_t)) {
                tcp_header_t* tcp = (tcp_header_t*)l4;
                tcp->dst_port = ip_htons(orig_src_port);
                tcp_fix_checksum(ip_htonl(ip->src_ip), orig_src, tcp, l4_len);
            } else if (proto == IP_PROTO_UDP && l4_len >= sizeof(udp_header_t)) {
                udp_header_t* udp = (udp_header_t*)l4;
                udp->dst_port = ip_htons(orig_src_port);
                udp_fix_checksum(ip_htonl(ip->src_ip), orig_src, udp, l4_len);
            } else {
                return -1;
            }

            /* Update conntrack timeout */
            ct->timeout_tick = now + (proto == IP_PROTO_TCP ? CT_TCP_EST_TIMEOUT : CT_UDP_TIMEOUT);
            if (ct->state == CT_NEW) ct->state = CT_ESTABLISHED;

            /* Forward to the original source */
            return (nat_forward_ip(l3_buf, l3_len, orig_src) == 0) ? 0 : -1;
        }

        /* ── Check NAT remapping for both SNAT and DNAT replies ── */
        for (u32 i = 0u; i < CONNTRACK_MAX; i++) {
            if (!g_ct[i].valid) continue;
            conntrack_entry_t* ct = &g_ct[i];
            if (proto != ct->key.proto) continue;

            int is_snat_reply = (dst_ip == ct->nat_src_ip && dst_port == ct->nat_src_port &&
                                 src_ip == ct->nat_dst_ip && src_port == ct->nat_dst_port);
            int is_dnat_reply = (src_ip == ct->nat_src_ip && src_port == ct->nat_src_port &&
                                 dst_ip == ct->nat_dst_ip && dst_port == ct->nat_dst_port);

            if (!is_snat_reply && !is_dnat_reply) continue;
            if (l3_len < sizeof(ipv4_header_t)) return -1;

            u32 orig_ip;
            u16 orig_port;
            u32 fwd_ip;
            ipv4_header_t* ip = (ipv4_header_t*)l3_buf;

            if (is_snat_reply) {
                /* ── Reverse SNAT: reply from external host → internal ── */
                orig_ip = ct->key.src_ip;
                orig_port = ct->key.src_port;
                fwd_ip = ct->key.src_ip;
            } else {
                /* ── Reverse DNAT: reply from internal host → external client ── */
                orig_ip = ct->key.dst_ip;
                orig_port = ct->key.dst_port;
                fwd_ip = ip_htonl(ip->dst_ip);
            }
            u16 ihl = (u16)((ip->ver_ihl & 0x0Fu) * 4u);
            u8* l4 = l3_buf + ihl;
            u16 l4_len = (u16)(l3_len - ihl);

            if (is_snat_reply) {
                /* Rewrite destination back to original source */
                ip->dst_ip = ip_htonl(orig_ip);
                ip_fix_checksum(ip);
                if (proto == IP_PROTO_TCP && l4_len >= sizeof(tcp_header_t)) {
                    ((tcp_header_t*)l4)->dst_port = ip_htons(orig_port);
                    tcp_fix_checksum(ip_htonl(ip->src_ip), orig_ip, (tcp_header_t*)l4, l4_len);
                } else if (proto == IP_PROTO_UDP && l4_len >= sizeof(udp_header_t)) {
                    ((udp_header_t*)l4)->dst_port = ip_htons(orig_port);
                    udp_fix_checksum(ip_htonl(ip->src_ip), orig_ip, (udp_header_t*)l4, l4_len);
                } else return -1;
            } else {
                /* Rewrite source back to our external IP */
                ip->src_ip = ip_htonl(orig_ip);
                ip_fix_checksum(ip);
                if (proto == IP_PROTO_TCP && l4_len >= sizeof(tcp_header_t)) {
                    ((tcp_header_t*)l4)->src_port = ip_htons(orig_port);
                    tcp_fix_checksum(orig_ip, ip_htonl(ip->dst_ip), (tcp_header_t*)l4, l4_len);
                } else if (proto == IP_PROTO_UDP && l4_len >= sizeof(udp_header_t)) {
                    ((udp_header_t*)l4)->src_port = ip_htons(orig_port);
                    udp_fix_checksum(orig_ip, ip_htonl(ip->dst_ip), (udp_header_t*)l4, l4_len);
                } else return -1;
            }

            ct->timeout_tick = now + (proto == IP_PROTO_TCP ? CT_TCP_EST_TIMEOUT : CT_UDP_TIMEOUT);
            if (ct->state == CT_NEW) ct->state = CT_ESTABLISHED;

            return (nat_forward_ip(l3_buf, l3_len, fwd_ip) == 0) ? 0 : -1;
        }
    }

    if (ct_idx >= 0) {
        /* Existing conntrack entry — just update timeout */
        g_ct[(u32)ct_idx].timeout_tick = now + (proto == IP_PROTO_TCP ? CT_TCP_EST_TIMEOUT : CT_UDP_TIMEOUT);
        if (g_ct[(u32)ct_idx].state == CT_NEW) g_ct[(u32)ct_idx].state = CT_ESTABLISHED;
        return 1; /* Continue to normal socket processing */
    }

    /* ─── Step 2: Check DNAT (Port Forwarding) rules ─── */
    /* This applies to packets destined to our external IP */

    for (u32 ri = 0u; ri < NAT_RULE_MAX; ri++) {
        if (!g_rules[ri].valid || g_rules[ri].type != NAT_TYPE_DNAT) continue;
        if (g_rules[ri].proto != 0u && g_rules[ri].proto != proto) continue;
        if (g_rules[ri].external_port != dst_port) continue;

        /* ── Apply DNAT: rewrite destination to internal host ── */
        ipv4_header_t* ip = (ipv4_header_t*)l3_buf;
        u16 ihl = (u16)((ip->ver_ihl & 0x0Fu) * 4u);
        u8* l4 = l3_buf + ihl;
        u16 l4_len = (u16)(l3_len - ihl);

        u32 int_ip  = g_rules[ri].internal_ip;
        u16 int_port = g_rules[ri].internal_port != 0u ? g_rules[ri].internal_port : dst_port;

        /* ── Create conntrack entry for the DNAT mapping ── */
        int slot = ct_alloc();
        if (slot < 0) return -1;
        conntrack_entry_t* ct = &g_ct[(u32)slot];

        ct->valid = 1u;
        ct->key.src_ip  = src_ip;
        ct->key.dst_ip  = dst_ip;
        ct->key.src_port = src_port;
        ct->key.dst_port = dst_port;
        ct->key.proto   = proto;
        ct->state = CT_NEW;
        ct->timeout_tick = now + (proto == IP_PROTO_TCP ? CT_TCP_NEW_TIMEOUT : CT_UDP_TIMEOUT);
        /* Save NAT mapping: internal host becomes the new destination */
        ct->nat_src_ip  = int_ip;
        ct->nat_src_port = int_port;
        ct->nat_dst_ip  = src_ip;
        ct->nat_dst_port = src_port;
        g_nat_st.conntrack_count++;

        /* ── Rewrite IP dest ── */
        ip->dst_ip = ip_htonl(int_ip);
        ip_fix_checksum(ip);

        /* ── Rewrite L4 dest port ── */
        if (proto == IP_PROTO_TCP && l4_len >= sizeof(tcp_header_t)) {
            tcp_header_t* tcp = (tcp_header_t*)l4;
            tcp->dst_port = ip_htons(int_port);
            tcp_fix_checksum(ip_htonl(ip->src_ip), int_ip, tcp, l4_len);
        } else if (proto == IP_PROTO_UDP && l4_len >= sizeof(udp_header_t)) {
            udp_header_t* udp = (udp_header_t*)l4;
            udp->dst_port = ip_htons(int_port);
            udp_fix_checksum(ip_htonl(ip->src_ip), int_ip, udp, l4_len);
        } else {
            g_ct[(u32)slot].valid = 0u;
            g_nat_st.conntrack_count--;
            return -1;
        }

        g_nat_st.dnat_packets++;

        /* ── Forward to internal host ── */
        return (nat_forward_ip(l3_buf, l3_len, int_ip) == 0) ? 0 : -1;
    }

    /* ─── Step 3: Check if this needs SNAT (outgoing from internal network) ─── */
    /* Only if IP forwarding is enabled and this is NOT for our IP */
    if (!g_nat_st.ip_forward) return 1; /* Continue normal processing */

    /* Check if src_ip is on an internal network that has SNAT rules */
    for (u32 ri = 0u; ri < NAT_RULE_MAX; ri++) {
        if (!g_rules[ri].valid || g_rules[ri].type != NAT_TYPE_SNAT) continue;
        if (g_rules[ri].proto != 0u && g_rules[ri].proto != proto) continue;

        /* Check if src_ip matches the internal network */
        if ((src_ip & g_rules[ri].internal_mask) != (g_rules[ri].internal_net & g_rules[ri].internal_mask)) continue;

        /* This packet is from our internal network, going to the outside.
           Apply SNAT: rewrite src to our external IP with an ephemeral port. */
        u32 ext_ip = netstack_local_ip();
        u16 eph_port = ct_ephemeral();

        if (l3_len < sizeof(ipv4_header_t)) return -1;
        ipv4_header_t* ip_s = (ipv4_header_t*)l3_buf;
        u16 ihl_s = (u16)((ip_s->ver_ihl & 0x0Fu) * 4u);
        u8* l4_s = l3_buf + ihl_s;
        u16 l4_len_s = (u16)(l3_len - ihl_s);

        /* Create conntrack entry */
        int s_slot = ct_alloc();
        if (s_slot < 0) return -1;
        conntrack_entry_t* s_ct = &g_ct[(u32)s_slot];
        s_ct->valid = 1u;
        s_ct->key.src_ip = src_ip;
        s_ct->key.dst_ip = dst_ip;
        s_ct->key.src_port = src_port;
        s_ct->key.dst_port = dst_port;
        s_ct->key.proto = proto;
        s_ct->state = CT_NEW;
        s_ct->timeout_tick = now + (proto == IP_PROTO_TCP ? CT_TCP_NEW_TIMEOUT : CT_UDP_TIMEOUT);
        s_ct->nat_src_ip = ext_ip;
        s_ct->nat_src_port = eph_port;
        s_ct->nat_dst_ip = dst_ip;
        s_ct->nat_dst_port = dst_port;
        g_nat_st.conntrack_count++;

        /* Rewrite source IP + port */
        ip_s->src_ip = ip_htonl(ext_ip);
        ip_fix_checksum(ip_s);

        if (proto == IP_PROTO_TCP && l4_len_s >= sizeof(tcp_header_t)) {
            tcp_header_t* tcp_s = (tcp_header_t*)l4_s;
            tcp_s->src_port = ip_htons(eph_port);
            tcp_fix_checksum(ext_ip, dst_ip, tcp_s, l4_len_s);
        } else if (proto == IP_PROTO_UDP && l4_len_s >= sizeof(udp_header_t)) {
            udp_header_t* udp_s = (udp_header_t*)l4_s;
            udp_s->src_port = ip_htons(eph_port);
            udp_fix_checksum(ext_ip, dst_ip, udp_s, l4_len_s);
        } else {
            s_ct->valid = 0u;
            g_nat_st.conntrack_count--;
            return -1;
        }

        g_nat_st.snat_packets++;

        /* Forward to external destination */
        ip_s->ttl--; /* Decrement TTL for forwarded packet */
        ip_fix_checksum(ip_s);
        return (nat_forward_ip(l3_buf, l3_len, dst_ip) == 0) ? 0 : -1;
    }

    return 1; /* Continue to normal socket processing */
}
