/* In-kernel WireGuard v1: Noise_IKpsk2 handshake state machine, transport
 * data with 2048-bit replay window, retry/rekey/keepalive timers, UDP glue.
 * Crypto core lives in crypto/noise_ik.c (host-testable). */

#include "wireguard.h"
#include "noise_ik.h"
#include "udp_raw.h"
#include "netstack.h"
#include "entropy.h"
#include "timer.h"
#include "rtc.h"
#include "serial.h"
#include "string.h"

#define WG_REKEY_AFTER_TICKS     (120u * timer_frequency())
#define WG_REJECT_AFTER_TICKS    (180u * timer_frequency())
#define WG_RETRY_INTERVAL_TICKS  (5u * timer_frequency())
#define WG_KEEPALIVE_TICKS       (10u * timer_frequency())
#define WG_FLOOD_GUARD_TICKS     timer_frequency()
#define WG_REPLAY_WINDOW_BITS    2048ull
#define WG_REPLAY_WINDOW_WORDS   32u

typedef struct {
    u64 w[WG_REPLAY_WINDOW_WORDS];
} wg_bitmap_t;

typedef struct {
    u8  valid;
    u8  we_initiated;
    u32 local_index;
    u32 remote_index;
    u8  send_key[NOISE_KEY_LEN];
    u8  recv_key[NOISE_KEY_LEN];
    u64 tx_counter;
    u64 last_rx_seq;
    wg_bitmap_t bm;
    u32 established_tick;
    u32 last_rx_tick;
    u32 last_tx_tick;
} wg_session_t;

typedef struct {
    u8 configured;
    u8 enabled;
    u32 endpoint_ip;
    u16 endpoint_port;
    u16 listen_port;
    u32 allowed_net;
    u32 allowed_mask;
    u8 my_priv[NOISE_KEY_LEN];
    u8 my_pub[NOISE_KEY_LEN];
    u8 peer_pub[NOISE_KEY_LEN];
    u8 psk[NOISE_KEY_LEN];
    u8 has_psk;

    noise_initiator_t initiator;
    u8 pending_initiation;
    u32 last_attempt_tick;

    noise_responder_t responder;
    u32 last_initiation_tick;

    u8 peer_last_timestamp[NOISE_TIMESTAMP_LEN];

    wg_session_t session;
    u32 next_local_index;

    u64 rx_packets;
    u64 tx_packets;
    u32 handshake_count;
    u32 replay_drops;
    u32 auth_failures;
    u8 role_last_handshake;
} wg_state_t;

static wg_state_t g_wg;

static void rng_fill(u8* out, u32 len) {
    entropy_fill(out, len);
}

static u32 load32_le(const void* p) {
    const u8* b = (const u8*)p;
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

static u64 load64_le(const void* p) {
    const u8* b = (const u8*)p;
    return (u64)b[0] | ((u64)b[1] << 8) | ((u64)b[2] << 16) | ((u64)b[3] << 24) |
           ((u64)b[4] << 32) | ((u64)b[5] << 40) | ((u64)b[6] << 48) |
           ((u64)b[7] << 56);
}

static void store_le32(void* p, u32 v) {
    u8* b = (u8*)p;
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    b[2] = (u8)(v >> 16);
    b[3] = (u8)(v >> 24);
}

static void store_le64(void* p, u64 v) {
    store_le32(p, (u32)(v & 0xFFFFFFFFu));
    store_le32((u8*)p + 4, (u32)(v >> 32));
}

static void store_be32(void* p, u32 v) {
    u8* b = (u8*)p;
    b[0] = (u8)(v >> 24);
    b[1] = (u8)(v >> 16);
    b[2] = (u8)(v >> 8);
    b[3] = (u8)v;
}

/* TAI64N: 8-byte big-endian seconds offset by 2^62 + 4-byte BE nanos */
static void tai64n_now(u8 out[NOISE_TIMESTAMP_LEN]) {
    rtc_time_t t;
    u64 epoch = 0ull;
    if (rtc_get_time(&t) == 0) {
        epoch = (u64)rtc_days_since_epoch(&t) * 86400ull +
                (u64)rtc_seconds_since_midnight(&t);
    }
    static u32 sub_nanos = 0u;
    sub_nanos += 1000000u; /* strictly monotonic within a boot */
    if (sub_nanos == 0u) sub_nanos = 1u;
    u64 tai = epoch + 0x4000000000000000ull;
    for (u32 i = 0; i < 8; i++) {
        out[i] = (u8)(tai >> (56u - (i * 8u)));
    }
    store_be32(out + 8, sub_nanos);
}

/* ---------------- replay window (2048 bits, distance-encoded) ----------------
 * Bit p of the bitmap represents sequence number last_rx_seq - p. */

static void bitmap_advance(wg_bitmap_t* b, u64 n) {
    if (n == 0ull) return;
    if (n >= WG_REPLAY_WINDOW_BITS) {
        memset(b->w, 0, sizeof(b->w));
        return;
    }
    u32 words = (u32)(n >> 6);
    u32 bits = (u32)(n & 63u);
    if (words > 0u) {
        for (i32 i = (i32)WG_REPLAY_WINDOW_WORDS - 1; i >= 0; i--) {
            i32 src = i - (i32)words;
            b->w[i] = (src >= 0) ? b->w[src] : 0ull;
        }
    }
    if (bits > 0u) {
        for (i32 i = (i32)WG_REPLAY_WINDOW_WORDS - 1; i >= 0; i--) {
            u64 carry = (i > 0) ? (b->w[i - 1] >> (64u - bits)) : 0ull;
            b->w[i] = (b->w[i] << bits) | carry;
        }
    }
}

static int replay_check_and_update(wg_session_t* s, u64 seq) {
    if (seq > s->last_rx_seq) {
        bitmap_advance(&s->bm, seq - s->last_rx_seq);
        s->bm.w[0] |= 1ull;
        s->last_rx_seq = seq;
        return 0;
    }
    u64 delta = s->last_rx_seq - seq;
    if (delta >= WG_REPLAY_WINDOW_BITS) return -1;
    if ((s->bm.w[delta >> 6] >> (delta & 63u)) & 1ull) return -1;
    s->bm.w[delta >> 6] |= 1ull << (delta & 63u);
    return 0;
}

/* ---------------- session management ---------------- */

static void zeroize(void* p, u32 len) {
    volatile u8* q = (volatile u8*)p;
    for (u32 i = 0; i < len; i++) q[i] = 0u;
}

static void session_reset(wg_session_t* s) {
    zeroize(s, sizeof(*s));
}

static void session_install(u32 local_index, u32 remote_index,
                            const u8 send_key[NOISE_KEY_LEN],
                            const u8 recv_key[NOISE_KEY_LEN],
                            int we_initiated) {
    wg_session_t* s = &g_wg.session;
    session_reset(s);
    s->valid = 1u;
    s->we_initiated = (u8)(we_initiated ? 1 : 0);
    s->local_index = local_index;
    s->remote_index = remote_index;
    memcpy(s->send_key, send_key, NOISE_KEY_LEN);
    memcpy(s->recv_key, recv_key, NOISE_KEY_LEN);
    s->tx_counter = 0ull;
    s->last_rx_seq = 0ull;
    s->established_tick = timer_ticks();
    s->last_rx_tick = s->established_tick;
    s->last_tx_tick = s->established_tick;
    g_wg.handshake_count++;
    g_wg.role_last_handshake = (u8)(we_initiated ? 1 : 2);
    g_wg.pending_initiation = 0u;
    g_wg.responder.in_use = 0u;
}

/* ---------------- wire format helpers ----------------
 * transport: [0]=4 [4]=receiver_le32 [8]=counter_le64 [16..]=ciphertext+tag */

static int udp_send_to_peer(const u8* payload, u16 len) {
    u32 src = netstack_local_ip();
    u16 sport = g_wg.listen_port;
    return udp_raw_send(src, g_wg.endpoint_ip, sport, g_wg.endpoint_port,
                        payload, len);
}

static void send_handshake_initiation(void) {
    u8 ts[NOISE_TIMESTAMP_LEN];
    u8 msg[WG_MSG_INITIATION_SIZE];
    tai64n_now(ts);
    noise_create_initiation(&g_wg.initiator,
                            g_wg.my_priv, g_wg.my_pub, g_wg.peer_pub,
                            g_wg.has_psk ? g_wg.psk : 0,
                            ts, rng_fill, msg);
    g_wg.pending_initiation = 1u;
    g_wg.last_attempt_tick = timer_ticks();
    (void)udp_send_to_peer(msg, WG_MSG_INITIATION_SIZE);
}

static void handle_initiation(const u8* data, u16 len) {
    u8 resp[WG_MSG_RESPONSE_SIZE];
    u8 send_key[NOISE_KEY_LEN], recv_key[NOISE_KEY_LEN];
    u8 peer_pub[NOISE_KEY_LEN];
    u8 ts[NOISE_TIMESTAMP_LEN];
    u32 sender_index = 0u;
    u32 now = timer_ticks();

    if (len < WG_MSG_INITIATION_SIZE) return;
    /* flood guard */
    if (g_wg.last_initiation_tick != 0u &&
        (u32)(now - g_wg.last_initiation_tick) < WG_FLOOD_GUARD_TICKS) {
        g_wg.auth_failures++;
        return;
    }

    if (noise_accept_initiation(&g_wg.responder, g_wg.my_priv, g_wg.my_pub,
                                g_wg.has_psk ? g_wg.psk : 0,
                                data, peer_pub, ts, &sender_index) != 0) {
        g_wg.auth_failures++;
        return;
    }
    /* timestamp must strictly advance vs previous handshake from this peer */
    for (u32 i = 0; i < NOISE_TIMESTAMP_LEN; i++) {
        if (ts[i] != g_wg.peer_last_timestamp[i]) {
            if (ts[i] < g_wg.peer_last_timestamp[i]) {
                g_wg.auth_failures++;
                zeroize(&g_wg.responder, sizeof(g_wg.responder));
                return;
            }
            break;
        }
    }
    memcpy(g_wg.peer_last_timestamp, ts, NOISE_TIMESTAMP_LEN);
    g_wg.last_initiation_tick = now;

    u32 local_index = ++g_wg.next_local_index;
    noise_create_response(&g_wg.responder, rng_fill, local_index,
                          resp, send_key, recv_key);
    session_install(local_index, sender_index, send_key, recv_key, 0);
    (void)udp_send_to_peer(resp, WG_MSG_RESPONSE_SIZE);
    serial_writeln("[WIREGUARD] handshake completed (responder role)");
}

static void handle_response(const u8* data, u16 len) {
    u8 send_key[NOISE_KEY_LEN], recv_key[NOISE_KEY_LEN];
    u32 remote_index = 0u;

    if (!g_wg.pending_initiation || len < WG_MSG_RESPONSE_SIZE) return;
    if (noise_consume_response(&g_wg.initiator, data,
                               send_key, recv_key, &remote_index) != 0) {
        g_wg.auth_failures++;
        g_wg.pending_initiation = 0u;
        return;
    }
    session_install(g_wg.initiator.sender_index, remote_index,
                    send_key, recv_key, 1);
    serial_writeln("[WIREGUARD] handshake completed (initiator role)");
}

static void handle_transport_data(const u8* data, u16 len) {
    const u32 head = 16u;
    if (len < head + NOISE_TAG_LEN) return;
    wg_session_t* s = &g_wg.session;
    if (!s->valid) return;
    if (load32_le(data + 4u) != s->local_index) return;

    u8 buf[1500];
    u32 ct_len = (u32)len - head;
    if (ct_len > sizeof(buf)) return;
    memcpy(buf, data + head, ct_len);

    u64 seq = load64_le(data + 8u);
    u32 plain_len = ct_len - NOISE_TAG_LEN;
    if (noise_aead_open(s->recv_key, seq, 0, 0u, buf,
                        plain_len, buf + plain_len) != 0) {
        g_wg.auth_failures++;
        return;
    }
    if (replay_check_and_update(s, seq) != 0) {
        g_wg.replay_drops++;
        return;
    }
    s->last_rx_tick = timer_ticks();
    g_wg.rx_packets++;

    if (plain_len > 0u) {
        serial_write("[WIREGUARD] decrypted ");
        serial_write_dec((u32)plain_len);
        serial_writeln(" bytes from peer");
    } else {
        /* keepalive: reply in kind to keep NAT mappings alive both ways */
        if ((u32)(timer_ticks() - s->last_tx_tick) >= WG_KEEPALIVE_TICKS) {
            (void)wireguard_send_data(0, 0);
        }
    }
}

/* ---------------- public API ---------------- */

int wireguard_init(void) {
    memset(&g_wg, 0, sizeof(g_wg));
    g_wg.listen_port = WIREGUARD_UDP_PORT;
    g_wg.next_local_index = 0xC0DE0000u;
    return 0;
}

int wireguard_configure(u32 endpoint_ip, u16 endpoint_port,
                        const u8 private_key[32], const u8 peer_public_key[32],
                        const u8 psk_or_null[32],
                        u32 allowed_net, u32 allowed_mask) {
    if (!private_key || !peer_public_key || endpoint_port == 0u ||
        allowed_mask == 0u) {
        return -1;
    }
    session_reset(&g_wg.session);
    zeroize(&g_wg.initiator, sizeof(g_wg.initiator));
    zeroize(&g_wg.responder, sizeof(g_wg.responder));
    g_wg.endpoint_ip = endpoint_ip;
    g_wg.endpoint_port = endpoint_port;
    memcpy(g_wg.my_priv, private_key, NOISE_KEY_LEN);
    noise_clamp_key(g_wg.my_priv);
    noise_x25519_pub(g_wg.my_pub, g_wg.my_priv);
    memcpy(g_wg.peer_pub, peer_public_key, NOISE_KEY_LEN);
    if (psk_or_null) {
        memcpy(g_wg.psk, psk_or_null, NOISE_KEY_LEN);
        g_wg.has_psk = 1u;
    } else {
        g_wg.has_psk = 0u;
    }
    g_wg.allowed_net = allowed_net & allowed_mask;
    g_wg.allowed_mask = allowed_mask;
    g_wg.configured = 1u;
    g_wg.enabled = 1u;
    return 0;
}

int wireguard_start(void) {
    if (!g_wg.configured) return -1;
    g_wg.enabled = 1u;
    if (!g_wg.session.valid) {
        send_handshake_initiation();
    }
    return 0;
}

int wireguard_stop(void) {
    g_wg.enabled = 0u;
    session_reset(&g_wg.session);
    g_wg.pending_initiation = 0u;
    zeroize(&g_wg.initiator, sizeof(g_wg.initiator));
    zeroize(&g_wg.responder, sizeof(g_wg.responder));
    return 0;
}

void wireguard_clear_keys(void) {
    volatile u8* p = (volatile u8*)&g_wg;
    for (u32 i = 0; i < sizeof(g_wg); i++) p[i] = 0u;
    g_wg.listen_port = WIREGUARD_UDP_PORT;
}

void wireguard_status(wireguard_status_t* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->configured = g_wg.configured;
    out->active = g_wg.session.valid ? 1u : 0u;
    out->role_last_handshake = g_wg.role_last_handshake;
    out->endpoint_ip = g_wg.endpoint_ip;
    out->endpoint_port = g_wg.endpoint_port;
    out->listen_port = g_wg.listen_port;
    out->allowed_network = g_wg.allowed_net;
    out->allowed_mask = g_wg.allowed_mask;
    if (out->active) {
        out->session_local_index[0] = (u8)(g_wg.session.local_index);
        out->session_local_index[1] = (u8)(g_wg.session.local_index >> 8);
        out->session_remote_index[0] = (u8)(g_wg.session.remote_index);
        out->session_remote_index[1] = (u8)(g_wg.session.remote_index >> 8);
        out->session_age_ticks = timer_ticks() - g_wg.session.established_tick;
    }
    out->tx_counter = g_wg.session.tx_counter;
    out->rx_packets = g_wg.rx_packets;
    out->tx_packets = g_wg.tx_packets;
    out->handshake_count = g_wg.handshake_count;
    out->replay_drops = g_wg.replay_drops;
    out->auth_failures = g_wg.auth_failures;
    out->last_handshake_tick = g_wg.session.established_tick;
}

u16 wireguard_listen_port(void) {
    return g_wg.listen_port;
}

u8 wireguard_active(void) {
    return g_wg.session.valid ? 1u : 0u;
}

int wireguard_on_udp(u32 src_ip, u16 src_port, u16 dst_port,
                     const u8* data, u16 len) {
    (void)src_port;
    if (!g_wg.configured || !data || len < 4u) return 0;
    if (dst_port != g_wg.listen_port) return 0;
    if (src_ip != g_wg.endpoint_ip && src_ip != 0u) return 0;

    switch (data[0]) {
    case 1u:
        handle_initiation(data, len);
        return 1;
    case 2u:
        handle_response(data, len);
        return 1;
    case 4u:
        handle_transport_data(data, len);
        return 1;
    default:
        return 1;
    }
}

void wireguard_tick(void) {
    if (!g_wg.configured || !g_wg.enabled) return;
    u32 now = timer_ticks();

    wg_session_t* s = &g_wg.session;
    if (s->valid) {
        if ((u32)(now - s->established_tick) > WG_REJECT_AFTER_TICKS) {
            serial_writeln("[WIREGUARD] session lifetime exceeded; rekeying");
            session_reset(s);
            send_handshake_initiation();
            return;
        }
        if ((u32)(now - s->last_tx_tick) >= WG_KEEPALIVE_TICKS &&
            (u32)(now - s->last_rx_tick) >= WG_KEEPALIVE_TICKS) {
            (void)wireguard_send_data(0, 0);
        }
    } else {
        if (!g_wg.pending_initiation ||
            (u32)(now - g_wg.last_attempt_tick) >= WG_RETRY_INTERVAL_TICKS) {
            serial_writeln("[WIREGUARD] initiating handshake");
            send_handshake_initiation();
        }
    }
}

int wireguard_send_data(const void* payload, u16 len) {
    static const u8 head_zero[16] = { 0 };
    u8 packet[16u + 1500u];

    if (!g_wg.session.valid) return -1;
    if (len > 1400u) return -2;
    wg_session_t* s = &g_wg.session;
    if (s->tx_counter + 0x40000000ull >= 0xFFFFFFFFFFFFFFFFull) {
        return -3; /* near exhaustion; rekey path will refresh */
    }

    memcpy(packet, head_zero, 16u);
    packet[0] = 4u;
    store_le32(packet + 4u, s->remote_index);
    store_le64(packet + 8u, s->tx_counter);

    if (payload && len > 0u) memcpy(packet + 16u, payload, len);
    noise_aead_seal(s->send_key, s->tx_counter, 0, 0u,
                    packet + 16u, len, packet + 16u + len);

    s->tx_counter++;
    s->last_tx_tick = timer_ticks();
    g_wg.tx_packets++;
    return udp_send_to_peer(packet, (u16)(16u + len + NOISE_TAG_LEN));
}
