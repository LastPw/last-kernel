#include "dhcp_server.h"
#include "netstack.h"
#include "udp_raw.h"
#include "ip.h"
#include "nic.h"
#include "ethernet.h"
#include "timer.h"
#include "string.h"
#include "dhcp.h"        /* shared DHCP wire-protocol constants (ports, magic, msg types, options) */

/* DHCP ports/magic/message-types/option codes come from dhcp.h, which is
   shared with the client so both sides use the same wire values. */

/* ─── State ───────────────────────────────────────────────────────────────── */

static u8              g_active = 0u;
static int             g_sock = -1;
static dhcp_lease_t    g_leases[DHCP_SRV_LEASE_MAX];
static dhcp_server_status_t g_st;
static u8              g_rx_buf[DHCP_SRV_PACKET_MAX];

/* Default lease: 6000 ticks ≈ 60 s (at 100 Hz) */
#define LEASE_DEFAULT_TICKS 6000u

/* ─── Byte-order helpers ──────────────────────────────────────────────────── */

static void put32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}
static u32 get32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static u16 get16(const u8* p) {
    return (u16)(((u16)p[0] << 8) | p[1]);
}

/* ─── DHCP option parser ──────────────────────────────────────────────────── */

static int find_option(const u8* pkt, u32 len, u8 wanted, u8* out, u8 out_cap) {
    u32 at = 240u;
    while (at < len) {
        u8 type = pkt[at++];
        if (type == 0u) continue;
        if (type == DHCP_OPT_END || at >= len) break;
        u8 olen = pkt[at++];
        if (at + olen > len) return -1;
        if (type == wanted) {
            if (olen > out_cap || !out) return -1;
            memcpy(out, pkt + at, olen);
            return (int)olen;
        }
        at += olen;
    }
    return -1;
}

/* ─── Lease management ────────────────────────────────────────────────────── */

static int lease_find_by_mac(const u8 mac[6]) {
    for (u32 i = 0u; i < DHCP_SRV_LEASE_MAX; i++) {
        if (!g_leases[i].valid) continue;
        u8 match = 1u;
        for (u32 j = 0u; j < 6u; j++) { if (g_leases[i].mac[j] != mac[j]) { match = 0u; break; } }
        if (match) return (int)i;
    }
    return -1;
}

static int lease_find_by_ip(u32 ip) {
    for (u32 i = 0u; i < DHCP_SRV_LEASE_MAX; i++) {
        if (g_leases[i].valid && g_leases[i].offered_ip == ip) return (int)i;
    }
    return -1;
}

static int lease_alloc(void) {
    /* First, find expired leases */
    u32 now = timer_ticks();
    for (u32 i = 0u; i < DHCP_SRV_LEASE_MAX; i++) {
        if (!g_leases[i].valid) return (int)i;
        if ((i32)(g_leases[i].lease_end_tick - now) <= 0) {
            g_leases[i].valid = 0u;
            g_st.leases_active--;
            return (int)i;
        }
    }
    return -1; /* table full */
}

/* Pick the next available IP from the pool */
static u32 pool_allocate(u32 exclude_ip) {
    /* Calculate network and broadcast addresses from the pool configuration.
     * The original logic compared against pool_start - 1 and pool_end + 1,
     * which are outside the iteration range and never match. */
    u32 network_addr = g_st.pool_start & g_st.subnet_mask;
    u32 broadcast_addr = network_addr | (~g_st.subnet_mask);

    for (u32 ip = g_st.pool_start; ip <= g_st.pool_end; ip++) {
        if (ip == exclude_ip) continue;
        /* Skip the actual network address and broadcast address */
        if (ip == network_addr) continue;
        if (ip == broadcast_addr) continue;
        if (lease_find_by_ip(ip) < 0) return ip;
    }
    return 0u; /* pool exhausted */
}

/* ─── Build DHCP response ─────────────────────────────────────────────────── */

static int dhcp_send_reply(u32 xid, u8 msg_type, u32 yiaddr, u32 client_ip,
                            u32 server_ip, const u8 client_mac[6], u32 dest_ip) {
    u8 pkt[DHCP_SRV_PACKET_MAX];
    u8 mac[6];
    u32 at = 240u;
    u32 now = timer_ticks();

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 2u;          /* BOOTREPLY */
    pkt[1] = 1u;          /* Ethernet */
    pkt[2] = 6u;          /* hlen */
    put32(pkt + 4u, xid);
    pkt[10] = 0x80u;      /* flags: broadcast */
    /* ciaddr for REQUEST */
    put32(pkt + 12u, client_ip);
    /* yiaddr */
    put32(pkt + 16u, yiaddr);
    /* siaddr (server IP) */
    put32(pkt + 20u, server_ip);
    /* chaddr */
    if (client_mac) memcpy(pkt + 28u, client_mac, 6u);

    /* Magic cookie */
    put32(pkt + 236u, DHCP_MAGIC);

    /* Options */
    pkt[at++] = DHCP_OPT_MSG_TYPE; pkt[at++] = 1u; pkt[at++] = msg_type;

    /* Server ID */
    pkt[at++] = DHCP_OPT_SERVER_ID; pkt[at++] = 4u;
    put32(pkt + at, server_ip); at += 4u;

    /* Lease time */
    pkt[at++] = DHCP_OPT_LEASE_TIME; pkt[at++] = 4u;
    put32(pkt + at, g_st.lease_time_ticks / (timer_frequency() ? timer_frequency() : 100u));
    at += 4u;

    if (msg_type == DHCP_OFFER || msg_type == DHCP_ACK) {
        /* Subnet mask */
        pkt[at++] = DHCP_OPT_SUBNET; pkt[at++] = 4u;
        put32(pkt + at, g_st.subnet_mask); at += 4u;

        /* Router */
        pkt[at++] = DHCP_OPT_ROUTER; pkt[at++] = 4u;
        put32(pkt + at, g_st.gateway); at += 4u;

        /* DNS */
        if (g_st.dns_server) {
            pkt[at++] = DHCP_OPT_DNS; pkt[at++] = 4u;
            put32(pkt + at, g_st.dns_server); at += 4u;
        }
    }

    pkt[at++] = DHCP_OPT_END;

    /* Send to the client — use broadcast for simplicity */
    ethernet_set_peer_mac((const u8[]){0xFFu,0xFFu,0xFFu,0xFFu,0xFFu,0xFFu});
    return udp_raw_send(server_ip, dest_ip, DHCP_SERVER_PORT, DHCP_CLIENT_PORT, pkt, (u16)at);
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

int dhcp_server_configure(u32 pool_start, u32 pool_end,
                           u32 subnet_mask, u32 gateway, u32 dns_server,
                           u32 lease_time_ticks) {
    if (pool_start == 0u || pool_end == 0u || pool_end < pool_start) return -1;
    /* Stop if running */
    if (g_active) dhcp_server_stop();

    g_st.pool_start  = pool_start;
    g_st.pool_end    = pool_end;
    g_st.subnet_mask = subnet_mask;
    g_st.gateway     = gateway;
    g_st.dns_server  = dns_server;
    g_st.lease_time_ticks = lease_time_ticks ? lease_time_ticks : LEASE_DEFAULT_TICKS;

    /* Clear lease table */
    for (u32 i = 0u; i < DHCP_SRV_LEASE_MAX; i++) g_leases[i].valid = 0u;
    g_st.leases_active = 0u;
    g_st.offers_sent = 0u;
    g_st.acks_sent = 0u;
    g_st.declines_received = 0u;

    return 0;
}

int dhcp_server_start(void) {
    if (g_active) return 0;
    if (g_st.pool_start == 0u) return -1; /* not configured */

    g_sock = netstack_socket_open(IP_PROTO_UDP, DHCP_SERVER_PORT, 0u, DHCP_CLIENT_PORT);
    if (g_sock < 0) return -1;
    g_active = 1u;
    return 0;
}

void dhcp_server_stop(void) {
    if (!g_active) return;
    if (g_sock >= 0) {
        netstack_socket_close((u32)g_sock);
        g_sock = -1;
    }
    g_active = 0u;
}

int dhcp_server_active(void) {
    return g_active ? 1 : 0;
}

void dhcp_server_poll(void) {
    u32 src_ip;
    u16 src_port;

    if (!g_active || g_sock < 0) return;

    int n = netstack_socket_recv((u32)g_sock, g_rx_buf, sizeof(g_rx_buf), &src_ip, &src_port);
    if (n < 240) return;                /* malformed */
    if (get32(g_rx_buf + 236u) != DHCP_MAGIC) return; /* not DHCP */

    u8 msg_type = 0u;
    if (find_option(g_rx_buf, (u32)n, DHCP_OPT_MSG_TYPE, &msg_type, 1u) != 1) return;
    if (msg_type != DHCP_DISCOVER && msg_type != DHCP_REQUEST &&
        msg_type != DHCP_DECLINE && msg_type != DHCP_RELEASE) return;

    u32 xid   = get32(g_rx_buf + 4u);
    u8* chaddr = g_rx_buf + 28u;
    u32 local_ip = netstack_local_ip();

    if (msg_type == DHCP_DISCOVER) {
        /* ── DISCOVER → OFFER ── */
        u32 requested = 0u;
        u8 opt[4];
        if (find_option(g_rx_buf, (u32)n, DHCP_OPT_REQUESTED_IP, opt, sizeof(opt)) == 4)
            requested = get32(opt);

        u32 offer_ip = 0u;

        /* Check if this MAC already has a lease */
        int existing = lease_find_by_mac(chaddr);
        if (existing >= 0) {
            offer_ip = g_leases[(u32)existing].offered_ip;
        } else if (requested && lease_find_by_ip(requested) < 0 &&
                   requested >= g_st.pool_start && requested <= g_st.pool_end) {
            offer_ip = requested;       /* honour the requested IP if available */
        } else {
            offer_ip = pool_allocate(0u);
        }

        if (offer_ip == 0u) return;     /* no IP available */

        /* Create a temporary lease (will be confirmed on REQUEST) */
        int slot = lease_find_by_mac(chaddr);
        if (slot < 0) slot = lease_alloc();
        if (slot < 0) return;

        g_leases[(u32)slot].valid = 1u;
        memcpy(g_leases[(u32)slot].mac, chaddr, 6u);
        g_leases[(u32)slot].offered_ip = offer_ip;
        g_leases[(u32)slot].lease_end_tick = timer_ticks() + g_st.lease_time_ticks;
        if (g_st.leases_active < DHCP_SRV_LEASE_MAX) g_st.leases_active++;

        if (dhcp_send_reply(xid, DHCP_OFFER, offer_ip, 0u, local_ip, chaddr, 0xFFFFFFFFu) == 0) {
            g_st.offers_sent++;
        }
        return;
    }

    if (msg_type == DHCP_REQUEST) {
        /* ── REQUEST → ACK (or NAK) ── */
        u32 requested = 0u;
        u8 opt[4];
        u32 server_id = 0u;
        if (find_option(g_rx_buf, (u32)n, DHCP_OPT_REQUESTED_IP, opt, sizeof(opt)) == 4)
            requested = get32(opt);
        if (find_option(g_rx_buf, (u32)n, DHCP_OPT_SERVER_ID, opt, sizeof(opt)) == 4)
            server_id = get32(opt);

        /* If server_id is set and it's not us, ignore */
        if (server_id != 0u && server_id != local_ip) return;

        /* If client already has an IP (ciaddr), use that */
        u32 ciaddr = get32(g_rx_buf + 12u);
        if (requested == 0u) requested = ciaddr;

        if (requested == 0u || requested < g_st.pool_start || requested > g_st.pool_end) {
            /* Send NAK */
            dhcp_send_reply(xid, DHCP_NAK, 0u, 0u, local_ip, chaddr, 0xFFFFFFFFu);
            return;
        }

        /* Create/renew lease */
        int slot = lease_find_by_mac(chaddr);
        if (slot < 0) {
            /* Check if IP is in use by another MAC */
            int ip_owner = lease_find_by_ip(requested);
            if (ip_owner >= 0) {
                dhcp_send_reply(xid, DHCP_NAK, 0u, 0u, local_ip, chaddr, 0xFFFFFFFFu);
                return;
            }
            slot = lease_alloc();
            if (slot < 0) return;
            g_leases[(u32)slot].valid = 1u;
            memcpy(g_leases[(u32)slot].mac, chaddr, 6u);
            if (g_st.leases_active < DHCP_SRV_LEASE_MAX) g_st.leases_active++;
        }
        g_leases[(u32)slot].offered_ip = requested;
        g_leases[(u32)slot].lease_end_tick = timer_ticks() + g_st.lease_time_ticks;

        if (dhcp_send_reply(xid, DHCP_ACK, requested, ciaddr, local_ip, chaddr, 0xFFFFFFFFu) == 0) {
            g_st.acks_sent++;
        }
        return;
    }

    if (msg_type == DHCP_DECLINE) {
        g_st.declines_received++;
        /* Free the offered IP lease for this MAC */
        int slot = lease_find_by_mac(chaddr);
        if (slot >= 0) {
            g_leases[(u32)slot].valid = 0u;
            if (g_st.leases_active > 0u) g_st.leases_active--;
        }
        return;
    }

    if (msg_type == DHCP_RELEASE) {
        int slot = lease_find_by_mac(chaddr);
        if (slot >= 0) {
            g_leases[(u32)slot].valid = 0u;
            if (g_st.leases_active > 0u) g_st.leases_active--;
        }
        return;
    }
}

int dhcp_server_lease_get(u32 index, dhcp_lease_t* out) {
    if (!out) return -1;
    u32 c = 0u;
    for (u32 i = 0u; i < DHCP_SRV_LEASE_MAX; i++) {
        if (!g_leases[i].valid) continue;
        if (c == index) { *out = g_leases[i]; return 0; }
        c++;
    }
    return -1;
}

u32 dhcp_server_lease_count(void) {
    return g_st.leases_active;
}

void dhcp_server_status(dhcp_server_status_t* out) {
    if (!out) return;
    *out = g_st;
}
