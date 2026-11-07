#include "dhcp.h"
#include "dns.h"
#include "netstack.h"
#include "udp_raw.h"
#include "ethernet.h"
#include "ip.h"
#include "nic.h"
#include "timer.h"
#include "string.h"
#include "serial.h"
#include "entropy.h"

/* DHCP wire-protocol constants come from dhcp.h (shared with the server). */

static u8 g_dhcp_ready = 0u;
static u32 g_server_ip = 0u;
static u32 g_dns_ip = 0u;

static void put32(u8* p, u32 v) {
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static u32 get32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u32 add_option(u8* p, u32 at, u8 type, const void* value, u8 len) {
    p[at++] = type;
    p[at++] = len;
    if (len && value) memcpy(p + at, value, len);
    return at + len;
}

static int dhcp_send(u32 xid, u8 message, u32 requested, u32 server) {
    u8 packet[548];
    u8 mac[6];
    u32 at = 240u;
    memset(packet, 0, sizeof(packet));
    ethernet_default_mac(mac, 0);
    packet[0] = 1u; packet[1] = 1u; packet[2] = 6u;
    put32(packet + 4u, xid);
    packet[10] = 0x80u; /* broadcast replies */
    memcpy(packet + 28u, mac, 6u);
    put32(packet + 236u, DHCP_MAGIC);
    packet[at++] = DHCP_OPT_MSG_TYPE; packet[at++] = 1u; packet[at++] = message;
    if (message == DHCP_REQUEST) {
        u8 ip[4];
        put32(ip, requested);
        at = add_option(packet, at, DHCP_OPT_REQUESTED_IP, ip, 4u);
        put32(ip, server);
        at = add_option(packet, at, DHCP_OPT_SERVER_ID, ip, 4u);
    }
    {
        u8 cid[7];
        cid[0] = 1u; /* Ethernet */
        memcpy(cid + 1, mac, 6);
        at = add_option(packet, at, 61u, cid, 7u);
    }
    {
        const u8 params[] = { DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS };
        at = add_option(packet, at, DHCP_OPT_PARAM_LIST, params, sizeof(params));
    }
    packet[at++] = DHCP_OPT_END;
    ethernet_set_peer_mac((const u8[]){0xFFu,0xFFu,0xFFu,0xFFu,0xFFu,0xFFu});
    return udp_raw_send(0u, 0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, (u16)at);
}

static int parse_options(const u8* packet, u32 len, u8 wanted, u8* out, u8 out_cap) {
    u32 at = 240u;
    while (at < len) {
        u8 type = packet[at++];
        if (type == 0u) continue;
        if (type == DHCP_OPT_END || at >= len) break;
        u8 olen = packet[at++];
        if (at + olen > len) return -1;
        if (type == wanted) {
            if (olen > out_cap || !out) return -1;
            memcpy(out, packet + at, olen);
            return olen;
        }
        at += olen;
    }
    return -1;
}

static int dhcp_wait(u32 sock, u8* out, u32 out_cap, u32 timeout, u32 xid, u8 type,
                     u32* offered, u32* server, u32* mask, u32* gw, u32* dns) {
    u32 start_ticks = timer_ticks();
    u32 deadline = start_ticks + timeout;
    u32 guard = (timeout ? timeout : 200u) * 500000u;
    while (guard-- != 0u) {
        if (timeout > 0 && timer_ticks() != start_ticks && (i32)(timer_ticks() - deadline) >= 0) {
            break;
        }
        u32 src = 0u;
        u16 port = 0u;
        (void)nic_poll(16u);
        int n = netstack_socket_recv(sock, out, (u16)out_cap, &src, &port);
        if (n < 240 || port != DHCP_SERVER_PORT || get32(out + 4u) != xid || out[236] != 0x63u) continue;
        u8 mt = 0u;
        if (parse_options(out, (u32)n, DHCP_OPT_MSG_TYPE, &mt, 1u) != 1 || mt != type) continue;
        if (offered) *offered = get32(out + 16u);
        u8 opt[4];
        if (server && parse_options(out, (u32)n, DHCP_OPT_SERVER_ID, opt, sizeof(opt)) == 4) *server = get32(opt);
        if (mask && parse_options(out, (u32)n, DHCP_OPT_SUBNET, opt, sizeof(opt)) == 4) *mask = get32(opt);
        if (gw && parse_options(out, (u32)n, DHCP_OPT_ROUTER, opt, sizeof(opt)) == 4) *gw = get32(opt);
        if (dns && parse_options(out, (u32)n, DHCP_OPT_DNS, opt, sizeof(opt)) == 4) *dns = get32(opt);
        return 0;
    }
    return -1;
}

int dhcp_configure(u32 timeout_ticks) {
    u8 packet[1024];
    /* The transaction ID is the only thing tying a DHCP reply to our request:
     * dhcp_wait() accepts any packet whose xid matches. Deriving it from
     * timer_ticks() XOR a constant let an off-path attacker who can estimate
     * boot time guess it and forge an OFFER/ACK, handing us their choice of
     * gateway and DNS server. Draw it from the entropy pool instead. */
    u32 xid;
    {
        u8 xb[4];
        entropy_fill(xb, sizeof(xb));
        xid = ((u32)xb[0] << 24) | ((u32)xb[1] << 16) |
              ((u32)xb[2] << 8)  | (u32)xb[3];
    }
    u32 offered = 0u, server = 0u, mask = 0u, gw = 0u, dns = 0u;
    int sock;
    g_dhcp_ready = 0u;
    if (!nic_ready()) return -1;
    sock = netstack_socket_open(IP_PROTO_UDP, DHCP_CLIENT_PORT, 0u, DHCP_SERVER_PORT);
    if (sock < 0) return -1;
    serial_writeln("[LastOS] DHCP discover");
    if (dhcp_send(xid, DHCP_DISCOVER, 0u, 0u) != 0 ||
        dhcp_wait((u32)sock, packet, sizeof(packet), timeout_ticks, xid, DHCP_OFFER,
                  &offered, &server, 0, 0, 0) != 0 || offered == 0u || server == 0u) {
        serial_writeln("[LastOS] DHCP offer timeout");
        (void)netstack_socket_close((u32)sock);
        return -1;
    }
    serial_writeln("[LastOS] DHCP offer received");
    if (dhcp_send(xid, DHCP_REQUEST, offered, server) != 0 ||
        dhcp_wait((u32)sock, packet, sizeof(packet), timeout_ticks, xid, DHCP_ACK,
                  &offered, &server, &mask, &gw, &dns) != 0) {
        serial_writeln("[LastOS] DHCP ack timeout");
        (void)netstack_socket_close((u32)sock);
        return -1;
    }
    (void)netstack_socket_close((u32)sock);
    if (!mask) mask = 0xFFFFFF00u;
    netstack_set_local(offered, mask, gw);
    g_server_ip = server;
    g_dns_ip = dns;
    if (dns != 0u) {
        dns_set_server(0, dns);
    }
    g_dhcp_ready = 1u;
    return 0;
}

int dhcp_ready(void) { return g_dhcp_ready ? 1 : 0; }
u32 dhcp_server_ip(void) { return g_server_ip; }
u32 dhcp_dns_ip(void) { return g_dns_ip; }
