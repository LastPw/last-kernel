#include "dns_server.h"
#include "dns.h"
#include "netstack.h"
#include "udp_raw.h"
#include "ip.h"
#include "checksum.h"
#include "timer.h"
#include "string.h"
#include "nic.h"
#include "ethernet.h"

/* ─── DNS wire-format helpers ─────────────────────────────────────────────── */
#define DNS_TYPE_A     1u
#define DNS_TYPE_AAAA 28u
#define DNS_CLASS_IN   1u

static u16 rd16(const u8* p) { return (u16)((u16)p[0] << 8u) | p[1]; }
static void wr16(u8* p, u16 v) { p[0] = (u8)(v >> 8u); p[1] = (u8)v; }
static u32 rd32(const u8* p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void wr32(u8* p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

/* ─── State ───────────────────────────────────────────────────────────────── */

static u8       g_active = 0u;
static int      g_sock = -1;
static u32      g_queries_total = 0u;
static u32      g_queries_cached = 0u;
static u32      g_queries_forwarded = 0u;
static u32      g_queries_failed = 0u;
static u32      g_queries_throttled = 0u;
static u8       g_rx_buf[DNS_SRV_PACKET_MAX];

/* ─── Rate limiting ────────────────────────────────────────────────────────
 * A per-source-IP token bucket. Each client starts with DNS_RL_BURST tokens
 * and regains one every DNS_RL_REFILL_TICKS, capped at the burst size; a
 * query costs one token and is dropped when the bucket is empty.
 *
 * This matters more here than the query count suggests: a cache miss calls
 * dns_resolve_ipv4/6, which blocks the poll loop waiting on an upstream
 * reply, so unbounded queries stall the whole netstack rather than merely
 * wasting cycles. Dropping silently (no reply) is deliberate — an error
 * response would let a spoofed source address turn this into a reflector.
 *
 * The table is small and evicts the stalest entry when full, so a spoofed
 * address flood degrades to a shared bucket instead of unbounded growth. */
#define DNS_RL_SLOTS         32u
#define DNS_RL_BURST         20u    /* queries a client may issue back-to-back */
#define DNS_RL_REFILL_TICKS  10u    /* ticks per regained token (~10/s at 100Hz) */

typedef struct {
    u8  used;
    u32 ip;
    u32 tokens;
    u32 last_refill_tick;
} dns_rl_entry_t;

static dns_rl_entry_t g_rl[DNS_RL_SLOTS];

/* Charge one token to `ip`. Returns 1 when the query may proceed, 0 to drop. */
static int rate_limit_allow(u32 ip) {
    u32 now = timer_ticks();
    u32 free_slot = DNS_RL_SLOTS;
    u32 oldest_slot = 0u;
    u32 oldest_age = 0u;

    for (u32 i = 0u; i < DNS_RL_SLOTS; i++) {
        if (!g_rl[i].used) {
            if (free_slot == DNS_RL_SLOTS) free_slot = i;
            continue;
        }
        if (g_rl[i].ip == ip) {
            /* Refill for elapsed time. The delta is taken as a signed
             * difference so it stays correct across tick wraparound, and a
             * negative delta (clock stepped back) counts as no elapsed time. */
            i32 delta = (i32)(now - g_rl[i].last_refill_tick);
            u32 elapsed = (delta > 0) ? (u32)delta : 0u;
            if (elapsed >= DNS_RL_REFILL_TICKS) {
                u32 gained = elapsed / DNS_RL_REFILL_TICKS;
                if (gained > DNS_RL_BURST) gained = DNS_RL_BURST;
                g_rl[i].tokens += gained;
                if (g_rl[i].tokens > DNS_RL_BURST) g_rl[i].tokens = DNS_RL_BURST;
                /* Keep the remainder so slow drips are not rounded away. */
                g_rl[i].last_refill_tick = now - (elapsed % DNS_RL_REFILL_TICKS);
            }
            if (g_rl[i].tokens == 0u) return 0;
            g_rl[i].tokens--;
            return 1;
        }
        /* Track the least recently refilled entry as the eviction candidate. */
        i32 age_delta = (i32)(now - g_rl[i].last_refill_tick);
        u32 age = (age_delta > 0) ? (u32)age_delta : 0u;
        if (age >= oldest_age) { oldest_age = age; oldest_slot = i; }
    }

    /* First query from this address: claim a slot, spending one token. */
    u32 slot = (free_slot != DNS_RL_SLOTS) ? free_slot : oldest_slot;
    g_rl[slot].used = 1u;
    g_rl[slot].ip = ip;
    g_rl[slot].tokens = DNS_RL_BURST - 1u;
    g_rl[slot].last_refill_tick = now;
    return 1;
}

static void rate_limit_reset(void) {
    for (u32 i = 0u; i < DNS_RL_SLOTS; i++) {
        g_rl[i].used = 0u;
        g_rl[i].ip = 0u;
        g_rl[i].tokens = 0u;
        g_rl[i].last_refill_tick = 0u;
    }
}

/* ─── Internal helpers ────────────────────────────────────────────────────── */

/* Copy and lower-case a DNS name from the wire into a plain dotted string.
   name_len is filled with the wire-bytes consumed (including the root label). */
static int decode_name(const u8* pkt, u32 pkt_len, u32* pos, char* out, u32 out_cap) {
    u32 at = *pos;
    u32 di = 0u;
    u32 guard = 0u;
    while (at < pkt_len && guard++ < 64u) {
        u8 len = pkt[at++];
        if (len == 0u) break;                       /* root label */
        if ((len & 0xC0u) == 0xC0u) {               /* pointer — skip over it, but we don't follow */
            if (at >= pkt_len) return -1;
            at++;
            break;
        }
        if (len > 63u || at + len > pkt_len) return -1;
        if (di && di < out_cap - 1u) out[di++] = '.';
        for (u32 i = 0u; i < len; i++) {
            char c = (char)pkt[at + i];
            if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
            /* Always consume the whole label: stopping the read cursor early
             * on a full output buffer would desync the wire parse and make the
             * next iteration read a payload byte as a label length. */
            if (di < out_cap - 1u) out[di++] = c;
        }
        at += len;
    }
    out[di < out_cap ? di : out_cap - 1u] = '\0';
    *pos = at;
    return 0;
}

/* Encode a dotted name into DNS wire format (labels + root).
   Returns the byte count written. */
static u32 encode_name(const char* name, u8* out, u32 cap) {
    u32 at = 0u;
    if (!name || !name[0]) return 0u;
    while (*name) {
        const char* dot = name;
        u32 len = 0u;
        while (*dot && *dot != '.') { dot++; len++; }
        if (len > 63u || at + len + 2u > cap) return 0u;
        out[at++] = (u8)len;
        for (u32 i = 0u; i < len; i++) out[at++] = (u8)name[i];
        name = *dot ? dot + 1u : dot;
    }
    if (at >= cap) return 0u;
    out[at++] = 0u; /* root */
    return at;
}

/* ─── Build a DNS response ──────────────────────────────────────────────────
 * Given the original query packet (id, query section), produces a response
 * with a single answer record.  Returns the response length, or 0 on error.
 */
static u32 build_response(const u8* query, u32 qlen, u16 qtype, u16 qclass,
                          const char* name, const u8* rdata, u16 rdlen, u32 ttl_ticks,
                          u8* resp, u32 cap) {
    u32 pos = 0u;
    u32 hz = timer_frequency();
    u32 ttl_sec = hz ? ttl_ticks / hz : 300u;
    u32 name_pos;                              /* position of question name for pointer */
    u32 src = 12u;                             /* read cursor into the query question section */
    (void)name;                                /* question name is copied verbatim from the query */

    if (cap < 12u + 4u + 10u + rdlen) return 0u; /* header + question + answer */

    /* Header */
    resp[pos++] = query[0];  resp[pos++] = query[1];   /* ID */
    resp[pos++] = 0x81u;                                /* flags: response, no error */
    resp[pos++] = 0x80u;                                /* flags[1]: RA set */
    /* flags = 0x8180 = response + RD + RA */
    resp[pos++] = 0x00u; resp[pos++] = 0x01u;            /* 1 question */
    resp[pos++] = 0x00u; resp[pos++] = 0x01u;            /* 1 answer */
    resp[pos++] = 0x00u; resp[pos++] = 0x00u;            /* 0 authority */
    resp[pos++] = 0x00u; resp[pos++] = 0x00u;            /* 0 additional */

    /* Question section — copy verbatim from query */
    name_pos = pos;
    /* Find the question name in the query (it starts at offset 12) */
    {
        u32 guard = 0u;
        while (src < qlen && guard++ < 128u) {
            u8 n = query[src];
            /* Every branch below writes into resp[]; the query is attacker
             * supplied, so each write needs its own bounds check. */
            if (n == 0u) { if (pos + 1u > cap) return 0u; resp[pos++] = 0u; src++; break; }
            if ((n & 0xC0u) == 0xC0u) {
                if (src + 1u >= qlen || pos + 2u > cap) return 0u;
                resp[pos++] = query[src];
                resp[pos++] = query[src + 1u];
                src += 2u;
                break;
            }
            if (src + 1u + n > qlen || pos + 1u + n >= cap) return 0u;
            resp[pos++] = n;
            src++;
            for (u32 i = 0u; i < n; i++) resp[pos++] = query[src++];
        }
    }
    /* Copy type + class from question */
    if (src + 4u > qlen || pos + 4u > cap) return 0u;
    u32 qtype_class = src;
    resp[pos++] = query[qtype_class];
    resp[pos++] = query[qtype_class + 1u];
    resp[pos++] = query[qtype_class + 2u];
    resp[pos++] = query[qtype_class + 3u];

    /* Answer section — encode using a pointer to the question name */
    if (pos + 2u + 8u + rdlen > cap) return 0u;
    wr16(resp + pos, (u16)(0xC000u | name_pos)); pos += 2u;  /* name pointer */
    wr16(resp + pos, qtype);  pos += 2u;                      /* type */
    wr16(resp + pos, qclass); pos += 2u;                      /* class */
    wr32(resp + pos, ttl_sec); pos += 4u;                     /* TTL */
    wr16(resp + pos, rdlen);  pos += 2u;                      /* data length */
    memcpy(resp + pos, rdata, rdlen); pos += rdlen;

    /* Flags already set to 0x8180 at the start — no fix needed. */

    return pos;
}

/* ─── Public API ──────────────────────────────────────────────────────────── */

int dns_server_start(void) {
    if (g_active) return 0;
    g_sock = netstack_socket_open(IP_PROTO_UDP, 53u, 0u, 0u);
    if (g_sock < 0) return -1;
    g_active = 1u;
    g_queries_total = 0u;
    g_queries_cached = 0u;
    g_queries_forwarded = 0u;
    g_queries_failed = 0u;
    g_queries_throttled = 0u;
    rate_limit_reset();
    return 0;
}

void dns_server_stop(void) {
    if (!g_active) return;
    if (g_sock >= 0) {
        netstack_socket_close((u32)g_sock);
        g_sock = -1;
    }
    g_active = 0u;
}

int dns_server_active(void) {
    return g_active ? 1 : 0;
}

void dns_server_poll(void) {
    u32 src_ip;
    u16 src_port;

    if (!g_active || g_sock < 0) return;

    int n = netstack_socket_recv((u32)g_sock, g_rx_buf, sizeof(g_rx_buf), &src_ip, &src_port);
    if (n < 12) return;
    /* src_ip = client IP, src_port = client ephemeral port (NOT 53).
       We respond to client_ip:client_port via udp_raw_send(). */

    /* Throttle before any parsing or upstream work. Dropped without a reply:
       see the rate-limiting comment above on reflection. */
    if (!rate_limit_allow(src_ip)) {
        g_queries_throttled++;
        return;
    }

    g_queries_total++;

    /* Parse the question section */
    u32 pos = 12u;
    char qname[128];
    if (decode_name(g_rx_buf, (u32)n, &pos, qname, sizeof(qname)) != 0 || pos + 4u > (u32)n) {
        g_queries_failed++;
        return;
    }
    u16 qtype  = rd16(g_rx_buf + pos);
    u16 qclass = rd16(g_rx_buf + pos + 2u);
    pos += 4u;

    if (qclass != DNS_CLASS_IN || (qtype != DNS_TYPE_A && qtype != DNS_TYPE_AAAA)) {
        /* Reply with "Not Implemented" */
        g_rx_buf[2] = 0x81u; g_rx_buf[3] = 0x84u; /* flags = response + notimp */
        g_rx_buf[6] = 0u; g_rx_buf[7] = 0u;         /* 0 answers */
        goto send_reply;
    }

    /* Try local cache first */
    {
        u8 addr[16];
        if (dns_cache_lookup(qname, (qtype == DNS_TYPE_A) ? 4u : 6u, addr) == 0) {
            u8 resp[DNS_SRV_PACKET_MAX];
            u32 rlen = build_response(g_rx_buf, (u32)n, qtype, qclass, qname,
                                       addr, (qtype == DNS_TYPE_A) ? 4u : 16u,
                                       3000u, resp, sizeof(resp));
            if (rlen > 0u) {
                if (udp_raw_send(netstack_local_ip(), src_ip, 53u, src_port, resp, (u16)rlen) == 0) {
                    g_queries_cached++;
                    return;
                }
            }
            g_queries_failed++;
            return;
        }
    }

    /* Not in cache — forward to upstream DNS server */
    {
        u32 upstream = 0u;
        for (u32 i = 0u; i < DNS_SERVER_MAX; i++) {
            if (dns_get_server(i, &upstream) == 0 && upstream != 0u) break;
        }
        if (upstream == 0u) {
            g_queries_failed++;
            return;
        }

        /* Use the existing dns client to resolve */
        u32 ipv4 = 0u;
        u8  ipv6[16];
        int ok = -1;
        if (qtype == DNS_TYPE_A) {
            ok = dns_resolve_ipv4(qname, &ipv4, 200u);
        } else {
            ok = dns_resolve_ipv6(qname, ipv6, 200u);
        }
        if (ok == 0) {
            u8 addr[16];
            if (qtype == DNS_TYPE_A) {
                addr[0] = (u8)(ipv4 >> 24); addr[1] = (u8)(ipv4 >> 16);
                addr[2] = (u8)(ipv4 >> 8);  addr[3] = (u8)ipv4;
            } else {
                memcpy(addr, ipv6, 16u);
            }
            u8 resp[DNS_SRV_PACKET_MAX];
            u32 rlen = build_response(g_rx_buf, (u32)n, qtype, qclass, qname,
                                       addr, (qtype == DNS_TYPE_A) ? 4u : 16u,
                                       3000u, resp, sizeof(resp));
            if (rlen > 0u && udp_raw_send(netstack_local_ip(), src_ip, 53u, src_port, resp, (u16)rlen) == 0) {
                g_queries_forwarded++;
                return;
            }
        }
    }

    g_queries_failed++;
    return;

send_reply:
    /* Send back a minimal error reply (already set up above, ID preserved). */
    if (n >= 12) {
        (void)udp_raw_send(netstack_local_ip(), src_ip, 53u, src_port, g_rx_buf, (u16)n);
    }
}

void dns_server_status(dns_server_status_t* out) {
    if (!out) return;
    out->active           = g_active ? 1u : 0u;
    out->queries_total    = g_queries_total;
    out->queries_cached   = g_queries_cached;
    out->queries_forwarded = g_queries_forwarded;
    out->queries_failed   = g_queries_failed;
    out->queries_throttled = g_queries_throttled;
}
