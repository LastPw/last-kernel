#include "dns.h"
#include "netstack.h"
#include "ip.h"
#include "nic.h"
#include "timer.h"
#include "string.h"
#include "entropy.h"
#include "dhcp.h"

#define DNS_PORT 53u
#define DNS_PACKET_MAX 256u
#define DNS_CACHE_MAX 16u
#define DNS_CACHE_TTL 3000u

static dns_cache_entry_t g_dns_cache[DNS_CACHE_MAX];
static u32 g_dns_servers[DNS_SERVER_MAX] = { 0u, 0x08080808u, 0x01010101u };

int dns_set_server(u32 index, u32 server_ip) {
    if (index >= DNS_SERVER_MAX || server_ip == 0u) return -1;
    g_dns_servers[index] = server_ip;
    dns_cache_clear();
    return 0;
}

int dns_get_server(u32 index, u32* out_server_ip) {
    if (index >= DNS_SERVER_MAX || !out_server_ip) return -1;
    *out_server_ip = g_dns_servers[index];
    return 0;
}

void dns_reset_servers(void) {
    g_dns_servers[0] = 0u;
    g_dns_servers[1] = 0x08080808u;
    g_dns_servers[2] = 0x01010101u;
    dns_cache_clear();
}

static u32 read_u32_be(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static int cache_name_equal(const char* a, const char* b) {
    u32 i;
    if (!a || !b) return 0;
    for (i = 0u; i + 1u < 64u; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
    }
    return 0;
}

void dns_cache_clear(void) { memset(g_dns_cache, 0, sizeof(g_dns_cache)); }

int dns_cache_lookup(const char* name, u8 family, u8* out_address) {
    u32 now = timer_ticks();
    if (!name || !name[0] || !out_address || (family != 4u && family != 6u)) return -1;
    for (u32 i = 0u; i < DNS_CACHE_MAX; i++) {
        if (g_dns_cache[i].valid && g_dns_cache[i].family == family &&
            cache_name_equal(g_dns_cache[i].name, name)) {
            if ((i32)(g_dns_cache[i].expires_tick - now) <= 0) {
                g_dns_cache[i].valid = 0u;
                continue;
            }
            memcpy(out_address, g_dns_cache[i].address, family == 4u ? 4u : 16u);
            return 0;
        }
    }
    return -1;
}

static void cache_store(const char* name, u8 family, const u8* address, u32 ttl_ticks) {
    u32 slot = DNS_CACHE_MAX;
    u32 oldest = 0u;
    u32 now = timer_ticks();
    if (!name || !address) return;
    for (u32 i = 0u; i < DNS_CACHE_MAX; i++) {
        if (!g_dns_cache[i].valid || (cache_name_equal(g_dns_cache[i].name, name) && g_dns_cache[i].family == family)) { slot = i; break; }
        if (g_dns_cache[i].expires_tick < g_dns_cache[oldest].expires_tick) oldest = i;
    }
    if (slot == DNS_CACHE_MAX) slot = oldest;
    memset(&g_dns_cache[slot], 0, sizeof(g_dns_cache[slot]));
    g_dns_cache[slot].valid = 1u;
    g_dns_cache[slot].family = family;
    str_ncpy(g_dns_cache[slot].name, name, sizeof(g_dns_cache[slot].name) - 1u);
    memcpy(g_dns_cache[slot].address, address, family == 4u ? 4u : 16u);
    if (ttl_ticks == 0u) ttl_ticks = DNS_CACHE_TTL;
    g_dns_cache[slot].expires_tick = now + ttl_ticks;
}

int dns_cache_get(u32 index, dns_cache_entry_t* out) {
    if (!out || index >= DNS_CACHE_MAX) return -1;
    if (!g_dns_cache[index].valid || (i32)(g_dns_cache[index].expires_tick - timer_ticks()) <= 0) return -1;
    memcpy(out, &g_dns_cache[index], sizeof(*out));
    return 0;
}

static int encode_name(const char* name, u8* out, u32 cap, u32* out_len) {
    u32 at = 1u;
    u32 label = 0u;
    if (!name || !name[0] || !out || cap < 2u) return -1;
    for (u32 i = 0u;; i++) {
        char c = name[i];
        if (c == '.' || c == '\0') {
            if (label == 0u || label > 63u || at + label + 1u >= cap) return -1;
            for (u32 j = at - label; j < at; j++) out[j] = out[j + 1u];
            out[at - label - 1u] = (u8)label;
            at++;
            if (c == '\0') break;
            label = 0u;
        } else {
            if (label >= 63u || at >= cap) return -1;
            out[at++] = (u8)c;
            label++;
        }
    }
    out[0] = (u8)(at - 1u);
    /* Re-encode cleanly because labels are easier to validate this way. */
    u32 src = 0u;
    at = 0u;
    while (name[src]) {
        u32 start = src;
        while (name[src] && name[src] != '.') src++;
        u32 len = src - start;
        if (len == 0u || len > 63u || at + len + 2u >= cap) return -1;
        u32 pos = at++;
        out[pos] = (u8)len;
        for (u32 i = 0u; i < len; i++) out[at++] = (u8)name[start + i];
        if (name[src] == '.') src++;
    }
    out[at++] = 0u;
    if (out_len) *out_len = at;
    return 0;
}

static int skip_name(const u8* p, u32 len, u32* pos) {
    u32 at = pos ? *pos : 0u;
    u32 guard = 0u;
    while (at < len && guard++ < 128u) {
        u8 n = p[at++];
        if (n == 0u) { if (pos) *pos = at; return 0; }
        if ((n & 0xC0u) == 0xC0u) {
            if (at >= len) return -1;
            at++;
            if (pos) *pos = at;
            return 0;
        }
        if (n > 63u || at + n > len) return -1;
        at += n;
    }
    return -1;
}

static int dns_query(u32 server, const char* name, u16 query_type, u8* out_addr,
                     u32 addr_len, u32 timeout_ticks, u32* out_ttl_ticks) {
    u8 query[DNS_PACKET_MAX];
    u8 response[DNS_PACKET_MAX];
    u32 qname_len = 0u;
    u32 pos = 12u;
    /* SECURITY: randomize source port to resist DNS cache poisoning
     * (Kaminsky attack). The old timer_ticks-based port was predictable. */
    u8 port_bytes[2];
    entropy_fill(port_bytes, sizeof(port_bytes));
    u32 local_port = (u32)(((u16)port_bytes[0] << 8u) | port_bytes[1]);
    local_port = (local_port % 16384u) + 49152u;
    int sock;
    memset(query, 0, sizeof(query));
    query[0] = (u8)(local_port >> 8u); query[1] = (u8)local_port;
    query[2] = 0x01u; query[5] = 0x01u;
    pos = 12u;
    if (encode_name(name, query + pos, DNS_PACKET_MAX - pos, &qname_len) != 0) return -1;
    pos += qname_len;
    if (pos + 4u > sizeof(query)) return -1;
    query[pos++] = (u8)(query_type >> 8); query[pos++] = (u8)query_type;
    query[pos++] = 0u; query[pos++] = 1u; /* IN */

    sock = netstack_socket_open(IP_PROTO_UDP, (u16)local_port, server, DNS_PORT);
    if (sock < 0 || netstack_socket_send((u32)sock, query, (u16)pos) != 0) {
        if (sock >= 0) (void)netstack_socket_close((u32)sock);
        return -1;
    }
    u32 deadline = timer_ticks() + (timeout_ticks ? timeout_ticks : 200u);
    int n = -1;
    while ((i32)(timer_ticks() - deadline) < 0) {
        (void)nic_poll(16u);
        n = netstack_socket_recv((u32)sock, response, sizeof(response), 0, 0);
        if (n >= 12) break;
    }
    (void)netstack_socket_close((u32)sock);
    /* A DNS response succeeds when QR is set and the low RCODE nibble is 0. */
    if (n < 12 || response[0] != query[0] || response[1] != query[1] ||
        (response[2] & 0x80u) == 0u || (response[3] & 0x0Fu) != 0u) return -1;
    u16 questions = (u16)(((u16)response[4] << 8) | response[5]);
    u16 answers = (u16)(((u16)response[6] << 8) | response[7]);
    if (questions == 0u || answers == 0u) return -1;
    pos = 12u;
    if (skip_name(response, (u32)n, &pos) != 0 || pos + 4u > (u32)n) return -1;
    pos += 4u;
    for (u32 i = 0u; i < answers && pos < (u32)n; i++) {
        if (skip_name(response, (u32)n, &pos) != 0 || pos + 10u > (u32)n) return -1;
        u16 type = (u16)(((u16)response[pos] << 8) | response[pos + 1u]);
        u16 cls = (u16)(((u16)response[pos + 2u] << 8) | response[pos + 3u]);
        u32 ttl = read_u32_be(response + pos + 4u);
        u16 data_len = (u16)(((u16)response[pos + 8u] << 8) | response[pos + 9u]);
        pos += 10u;
        if (pos + data_len > (u32)n) return -1;
        if (type == query_type && cls == 1u && data_len == addr_len) {
            if (out_addr) memcpy(out_addr, response + pos, addr_len);
            if (out_ttl_ticks) {
                u32 hz = timer_frequency();
                if (ttl > 86400u) ttl = 86400u;
                *out_ttl_ticks = ttl ? ttl * hz : DNS_CACHE_TTL;
            }
            return 0;
        }
        pos += data_len;
    }
    return -1;
}

int dns_resolve_ipv4(const char* name, u32* out_ip, u32 timeout_ticks) {
    u8 addr[4];
    u32 ttl_ticks = DNS_CACHE_TTL;
    if (!name || !name[0] || !out_ip) return -1;
    if (dns_cache_lookup(name, 4u, addr) == 0) { *out_ip = read_u32_be(addr); return 0; }
    for (u32 i = 0u; i < DNS_SERVER_MAX; i++) {
        u32 server = g_dns_servers[i];
        if (server != 0u && dns_query(server, name, 1u, addr, 4u, timeout_ticks, &ttl_ticks) == 0) {
            cache_store(name, 4u, addr, ttl_ticks); *out_ip = read_u32_be(addr); return 0;
        }
    }
    if (g_dns_servers[0] == 0u && dhcp_dns_ip() != 0u &&
        dns_query(dhcp_dns_ip(), name, 1u, addr, 4u, timeout_ticks, &ttl_ticks) == 0) {
        cache_store(name, 4u, addr, ttl_ticks); *out_ip = read_u32_be(addr); return 0;
    }
    return -1;
}

int dns_resolve_ipv6(const char* name, u8 out_ip[16], u32 timeout_ticks) {
    u32 ttl_ticks = DNS_CACHE_TTL;
    if (!name || !name[0] || !out_ip) return -1;
    if (dns_cache_lookup(name, 6u, out_ip) == 0) return 0;
    for (u32 i = 0u; i < DNS_SERVER_MAX; i++) {
        u32 server = g_dns_servers[i];
        if (server != 0u && dns_query(server, name, 28u, out_ip, 16u, timeout_ticks, &ttl_ticks) == 0) {
            cache_store(name, 6u, out_ip, ttl_ticks); return 0;
        }
    }
    if (g_dns_servers[0] == 0u && dhcp_dns_ip() != 0u &&
        dns_query(dhcp_dns_ip(), name, 28u, out_ip, 16u, timeout_ticks, &ttl_ticks) == 0) {
        cache_store(name, 6u, out_ip, ttl_ticks); return 0;
    }
    return -1;
}
