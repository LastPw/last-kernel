/* WireGuard Noise_IKpsk2 handshake and transport cryptography core.
 * Implements the exact sequence from the WireGuard whitepaper as mirrored
 * by wireguard-go: HMAC-BLAKE2s KDF, mixKey/mixHash transcript, es/ss/ee/se
 * DH mixes, PSK mixing during response, ChaCha20-Poly1305 with zero nonce
 * for handshake frames. Primitives come from BLAKE2s (local) + BearSSL.
 *
 * Pure computation only: no kernel/net dependencies, host-testable.
 * Curve25519 keys use RFC 7748 little-endian wire encoding. */

#include "noise_ik.h"
#include "string.h"


#include "blake2s.h"
#include "bearssl_ec.h"
#include "bearssl_block.h"

#define CURVE25519_ID BR_EC_curve25519

/* ---------------- internal helpers ---------------- */

static void store32_le(void* p, u32 v) {
    u8* b = (u8*)p;
    b[0] = (u8)v;
    b[1] = (u8)(v >> 8);
    b[2] = (u8)(v >> 16);
    b[3] = (u8)(v >> 24);
}

static u32 load32_le(const void* p) {
    const u8* b = (const u8*)p;
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

static void store64_le(void* p, u64 v) {
    store32_le(p, (u32)(v & 0xFFFFFFFFu));
    store32_le((u8*)p + 4, (u32)(v >> 32));
}

static int ct_equal(const void* a, const void* b, u32 len) {
    const u8* x = (const u8*)a;
    const u8* y = (const u8*)b;
    u8 diff = 0u;
    for (u32 i = 0; i < len; i++) diff |= (u8)(x[i] ^ y[i]);
    return diff == 0u;
}

void noise_clamp_key(u8 priv_le[NOISE_KEY_LEN]) {
    priv_le[0] &= 248u;
    priv_le[31] = (u8)((priv_le[31] & 127u) | 64u);
}

void noise_x25519_pub(u8 out_pub[NOISE_KEY_LEN], const u8 priv_le[NOISE_KEY_LEN]) {
    br_ec_c25519_i31.mulgen(out_pub, priv_le, NOISE_KEY_LEN, CURVE25519_ID);
}

void noise_x25519_shared(u8 out_shared[NOISE_KEY_LEN],
                         const u8 priv_le[NOISE_KEY_LEN],
                         const u8 peer_pub_le[NOISE_KEY_LEN]) {
    u8 point[NOISE_KEY_LEN];
    memcpy(point, peer_pub_le, NOISE_KEY_LEN);
    if (br_ec_c25519_i31.mul(point, NOISE_KEY_LEN,
                             priv_le, NOISE_KEY_LEN, CURVE25519_ID) != 1u) {
        memset(out_shared, 0, NOISE_KEY_LEN);
        return;
    }
    memcpy(out_shared, point, NOISE_KEY_LEN);
}

/* ---------------- KDF (HMAC-BLAKE2s, RFC 5869 style) ---------------- */

void noise_kdf1(u8 t0[NOISE_KEY_LEN], const u8 key[NOISE_KEY_LEN],
                const void* m, u32 m_len) {
    static const u8 one[1] = { 0x01 };
    hmac_blake2s(key, NOISE_KEY_LEN, m, m_len, t0);
    hmac_blake2s(t0, NOISE_KEY_LEN, one, 1u, t0);
}

void noise_kdf2(u8 t0[NOISE_KEY_LEN], u8 t1[NOISE_KEY_LEN],
                const u8 key[NOISE_KEY_LEN], const void* m, u32 m_len) {
    static const u8 one[1] = { 0x01 };
    static const u8 two[1] = { 0x02 };
    u8 prk[NOISE_KEY_LEN];
    u8 buf[NOISE_KEY_LEN + 1u];
    hmac_blake2s(key, NOISE_KEY_LEN, m, m_len, prk);
    hmac_blake2s(prk, NOISE_KEY_LEN, one, 1u, t0);
    memcpy(buf, t0, NOISE_KEY_LEN);
    buf[NOISE_KEY_LEN] = two[0];
    hmac_blake2s(prk, NOISE_KEY_LEN, buf, sizeof(buf), t1);
}

void noise_kdf3(u8 t0[NOISE_KEY_LEN], u8 t1[NOISE_KEY_LEN], u8 t2[NOISE_KEY_LEN],
                const u8 key[NOISE_KEY_LEN], const void* m, u32 m_len) {
    static const u8 one_buf[1] = { 0x01 };
    static const u8 two[1] = { 0x02 };
    static const u8 three[1] = { 0x03 };
    u8 prk[NOISE_KEY_LEN];
    u8 buf[NOISE_KEY_LEN + 1u];
    hmac_blake2s(key, NOISE_KEY_LEN, m, m_len, prk);
    hmac_blake2s(prk, NOISE_KEY_LEN, one_buf, 1u, t0);
    memcpy(buf, t0, NOISE_KEY_LEN);
    buf[NOISE_KEY_LEN] = two[0];
    hmac_blake2s(prk, NOISE_KEY_LEN, buf, sizeof(buf), t1);
    memcpy(buf, t1, NOISE_KEY_LEN);
    buf[NOISE_KEY_LEN] = three[0];
    hmac_blake2s(prk, NOISE_KEY_LEN, buf, sizeof(buf), t2);
}

/* mixHash: h' = BLAKE2s(h || data); mixKey: c' = KDF1(c, data) */
static void mix_hash(u8 h[NOISE_KEY_LEN], const void* data, u32 len) {
    blake2s_ctx ctx;
    blake2s_init(&ctx);
    blake2s_update(&ctx, h, NOISE_KEY_LEN);
    blake2s_update(&ctx, data, len);
    blake2s_final(&ctx, h);
}

static void mix_key(u8 c[NOISE_KEY_LEN], const void* data, u32 len) {
    u8 out[NOISE_KEY_LEN];
    noise_kdf1(out, c, data, len);
    memcpy(c, out, NOISE_KEY_LEN);
}

static void initial_chain_and_hash(u8 chain[NOISE_KEY_LEN], u8 hash[NOISE_KEY_LEN]) {
    blake2s(WG_CONSTRUCTION, 38u, chain);
    memcpy(hash, chain, NOISE_KEY_LEN);
    mix_hash(hash, WG_IDENTIFIER, 34u);
}

/* ---------------- AEAD / MAC ---------------- */

void noise_aead_seal(const u8 key[NOISE_KEY_LEN], u64 counter,
                     const void* ad, u32 ad_len,
                     void* data, u32 data_len, u8 tag[NOISE_TAG_LEN]) {
    u8 nonce[12];
    memset(nonce, 0, sizeof(nonce));
    store64_le(nonce + 4, counter);
    br_poly1305_ctmul_run(key, nonce, data, data_len, ad, ad_len,
                          tag, br_chacha20_ct_run, 1);
}

int noise_aead_open(const u8 key[NOISE_KEY_LEN], u64 counter,
                    const void* ad, u32 ad_len,
                    void* data, u32 data_len, const u8 tag[NOISE_TAG_LEN]) {
    u8 nonce[12];
    u8 computed[NOISE_TAG_LEN];
    memset(nonce, 0, sizeof(nonce));
    store64_le(nonce + 4, counter);
    br_poly1305_ctmul_run(key, nonce, data, data_len, ad, ad_len,
                          computed, br_chacha20_ct_run, 0);
    return ct_equal(computed, tag, NOISE_TAG_LEN) ? 0 : -1;
}

void noise_mac1(u8 mac[NOISE_MAC1_LEN], const u8 peer_static_pub[NOISE_KEY_LEN],
                const void* msg, u32 mac_len) {
    u8 key[NOISE_KEY_LEN];
    u8 label[8u + NOISE_KEY_LEN];
    memcpy(label, WG_LABEL_MAC1, 8u);
    memcpy(label + 8, peer_static_pub, NOISE_KEY_LEN);
    blake2s(label, sizeof(label), key);
    blake2s_keyed_ex(mac, NOISE_MAC1_LEN, key, NOISE_KEY_LEN, msg, mac_len);
}

int noise_mac1_verify(const u8 expected[NOISE_MAC1_LEN],
                      const u8 peer_static_pub[NOISE_KEY_LEN],
                      const void* msg, u32 mac_len) {
    u8 mac[NOISE_MAC1_LEN];
    noise_mac1(mac, peer_static_pub, msg, mac_len);
    return ct_equal(mac, expected, NOISE_MAC1_LEN) ? 0 : -1;
}

/* ---------------- handshake message layout ----------------
 * type1 (148B): [0]=type [4]=sender_le32 [8..39]=ephemeral
 *               [40..87]=enc_static(32+16) [88..115]=enc_timestamp(12+16)
 *               [116..131]=mac1 [132..147]=mac2(zeros)
 * type2 (92B):  [0]=type [4]=sender [8]=receiver [12..43]=ephemeral
 *               [44..59]=enc_empty(16) [60..75]=mac1 [76..91]=mac2(zeros)
 */
#define HS1_OFF_EPHEMERAL   8u
#define HS1_OFF_STATIC     40u
#define HS1_OFF_TIMESTAMP  88u
#define HS1_OFF_MAC1      116u
#define HS1_MAC_LEN       116u
#define HS2_OFF_EPHEMERAL  12u
#define HS2_OFF_EMPTY      44u
#define HS2_OFF_MAC1       60u
#define HS2_MAC_LEN        60u

/* ---------------- initiator ---------------- */

void noise_create_initiation(noise_initiator_t* st,
                             const u8 my_static_priv[NOISE_KEY_LEN],
                             const u8 my_static_pub[NOISE_KEY_LEN],
                             const u8 peer_static_pub[NOISE_KEY_LEN],
                             const u8 psk_or_null[NOISE_KEY_LEN],
                             const u8 timestamp[NOISE_TIMESTAMP_LEN],
                             void (*rng)(u8* out, u32 len),
                             u8 out_msg[WG_MSG_INITIATION_SIZE]) {
    u8 ss[NOISE_KEY_LEN];
    u8 key[NOISE_KEY_LEN];
    u8 chain[NOISE_KEY_LEN], hash[NOISE_KEY_LEN];

    memset(out_msg, 0, WG_MSG_INITIATION_SIZE);
    out_msg[0] = 1u;

    rng(st->eph_priv, NOISE_KEY_LEN);
    noise_clamp_key(st->eph_priv);
    noise_x25519_pub(out_msg + HS1_OFF_EPHEMERAL, st->eph_priv);

    st->sender_index = load32_le(out_msg + HS1_OFF_EPHEMERAL);
    store32_le(out_msg + 4u, st->sender_index);
    memcpy(st->my_static_priv, my_static_priv, NOISE_KEY_LEN);
    memcpy(st->peer_static_pub, peer_static_pub, NOISE_KEY_LEN);
    if (psk_or_null) memcpy(st->psk, psk_or_null, NOISE_KEY_LEN);
    else memset(st->psk, 0, NOISE_KEY_LEN);

    initial_chain_and_hash(chain, hash);
    /* Responder-side mirror: their public identity enters the transcript first */
    mix_hash(hash, peer_static_pub, NOISE_KEY_LEN);

    mix_key(chain, out_msg + HS1_OFF_EPHEMERAL, NOISE_KEY_LEN);
    mix_hash(hash, out_msg + HS1_OFF_EPHEMERAL, NOISE_KEY_LEN);

    /* es: ephemeral x responder static; seal our static identity */
    noise_x25519_shared(ss, st->eph_priv, peer_static_pub);
    noise_kdf2(chain, key, chain, ss, NOISE_KEY_LEN);
    memcpy(out_msg + HS1_OFF_STATIC, my_static_pub, NOISE_KEY_LEN);
    noise_aead_seal(key, 0u, hash, NOISE_KEY_LEN,
                    out_msg + HS1_OFF_STATIC, NOISE_KEY_LEN,
                    out_msg + HS1_OFF_STATIC + NOISE_KEY_LEN);
    mix_hash(hash, out_msg + HS1_OFF_STATIC, NOISE_KEY_LEN + NOISE_TAG_LEN);

    /* ss: static x static; seal TAI64N timestamp */
    noise_x25519_shared(ss, my_static_priv, peer_static_pub);
    noise_kdf2(chain, key, chain, ss, NOISE_KEY_LEN);
    memcpy(out_msg + HS1_OFF_TIMESTAMP, timestamp, NOISE_TIMESTAMP_LEN);
    noise_aead_seal(key, 0u, hash, NOISE_KEY_LEN,
                    out_msg + HS1_OFF_TIMESTAMP, NOISE_TIMESTAMP_LEN,
                    out_msg + HS1_OFF_TIMESTAMP + NOISE_TIMESTAMP_LEN);
    mix_hash(hash, out_msg + HS1_OFF_TIMESTAMP, NOISE_TIMESTAMP_LEN + NOISE_TAG_LEN);

    noise_mac1(out_msg + HS1_OFF_MAC1, peer_static_pub, out_msg, HS1_MAC_LEN);

    memcpy(st->chaining_key, chain, NOISE_KEY_LEN);
    memcpy(st->transcript_hash, hash, NOISE_KEY_LEN);
    st->in_use = 1u;
}

int noise_consume_response(noise_initiator_t* st,
                           const u8 msg[WG_MSG_RESPONSE_SIZE],
                           u8 send_key[NOISE_KEY_LEN],
                           u8 recv_key[NOISE_KEY_LEN],
                           u32* remote_index_out) {
    u8 ss[NOISE_KEY_LEN];
    u8 t0[NOISE_KEY_LEN], t1[NOISE_KEY_LEN], key[NOISE_KEY_LEN];
    u8 chain[NOISE_KEY_LEN], hash[NOISE_KEY_LEN];

    if (!st->in_use || msg == 0) return -1;
    if (msg[0] != 2u) return -2;
    if (load32_le(msg + 8u) != st->sender_index) return -3;
    memcpy(chain, st->chaining_key, NOISE_KEY_LEN);
    memcpy(hash, st->transcript_hash, NOISE_KEY_LEN);

    mix_key(chain, msg + HS2_OFF_EPHEMERAL, NOISE_KEY_LEN);
    mix_hash(hash, msg + HS2_OFF_EPHEMERAL, NOISE_KEY_LEN);

    /* ee */
    noise_x25519_shared(ss, st->eph_priv, msg + HS2_OFF_EPHEMERAL);
    mix_key(chain, ss, NOISE_KEY_LEN);
    /* se: initiator static x responder ephemeral */
    noise_x25519_shared(ss, st->my_static_priv, msg + HS2_OFF_EPHEMERAL);
    mix_key(chain, ss, NOISE_KEY_LEN);

    /* psk mix */
    noise_kdf3(t0, t1, key, chain, st->psk, NOISE_KEY_LEN);
    memcpy(chain, t0, NOISE_KEY_LEN);
    mix_hash(hash, t1, NOISE_KEY_LEN);


    if (noise_aead_open(key, 0u, hash, NOISE_KEY_LEN,
                        (void*)(msg + HS2_OFF_EMPTY), 0u,
                        msg + HS2_OFF_EMPTY) != 0) {
        return -4;
    }
    mix_hash(hash, msg + HS2_OFF_EMPTY, NOISE_TAG_LEN);

    noise_kdf2(send_key, recv_key, chain, 0, 0u);

    if (remote_index_out) *remote_index_out = load32_le(msg + 4u);
    memset(st, 0, sizeof(*st));
    return 0;
}

/* ---------------- responder ---------------- */

int noise_accept_initiation(noise_responder_t* st,
                            const u8 my_static_priv[NOISE_KEY_LEN],
                            const u8 my_static_pub[NOISE_KEY_LEN],
                            const u8 psk_or_null[NOISE_KEY_LEN],
                            const u8 msg[WG_MSG_INITIATION_SIZE],
                            u8 peer_static_pub_out[NOISE_KEY_LEN],
                            u8 timestamp_out[NOISE_TIMESTAMP_LEN],
                            u32* sender_index_out) {
    u8 ss[NOISE_KEY_LEN];
    u8 key[NOISE_KEY_LEN];
    u8 chain[NOISE_KEY_LEN], hash[NOISE_KEY_LEN];

    if (msg == 0 || msg[0] != 1u) return -1;

    if (noise_mac1_verify(msg + HS1_OFF_MAC1, my_static_pub, msg, HS1_MAC_LEN) != 0) {
        return -2;
    }

    initial_chain_and_hash(chain, hash);
    mix_hash(hash, my_static_pub, NOISE_KEY_LEN);
    mix_hash(hash, msg + HS1_OFF_EPHEMERAL, NOISE_KEY_LEN);
    mix_key(chain, msg + HS1_OFF_EPHEMERAL, NOISE_KEY_LEN);

    /* es mirrored: our static x their ephemeral; open their static identity */
    noise_x25519_shared(ss, my_static_priv, msg + HS1_OFF_EPHEMERAL);
    noise_kdf2(chain, key, chain, ss, NOISE_KEY_LEN);
    memcpy(st->peer_static_pub, msg + HS1_OFF_STATIC, NOISE_KEY_LEN);
    if (noise_aead_open(key, 0u, hash, NOISE_KEY_LEN,
                        st->peer_static_pub, NOISE_KEY_LEN,
                        msg + HS1_OFF_STATIC + NOISE_KEY_LEN) != 0) {
        return -3;
    }
    mix_hash(hash, msg + HS1_OFF_STATIC, NOISE_KEY_LEN + NOISE_TAG_LEN);

    /* ss mirrored */
    noise_x25519_shared(ss, my_static_priv, st->peer_static_pub);
    noise_kdf2(chain, key, chain, ss, NOISE_KEY_LEN);
    memcpy(timestamp_out, msg + HS1_OFF_TIMESTAMP, NOISE_TIMESTAMP_LEN);
    if (noise_aead_open(key, 0u, hash, NOISE_KEY_LEN,
                        timestamp_out, NOISE_TIMESTAMP_LEN,
                        msg + HS1_OFF_TIMESTAMP + NOISE_TIMESTAMP_LEN) != 0) {
        return -4;
    }
    mix_hash(hash, msg + HS1_OFF_TIMESTAMP, NOISE_TIMESTAMP_LEN + NOISE_TAG_LEN);

    memcpy(st->chain_key, chain, NOISE_KEY_LEN);
    memcpy(st->hash_state, hash, NOISE_KEY_LEN);
    memcpy(st->peer_eph_pub, msg + HS1_OFF_EPHEMERAL, NOISE_KEY_LEN);
    if (psk_or_null) memcpy(st->psk, psk_or_null, NOISE_KEY_LEN);
    else memset(st->psk, 0, NOISE_KEY_LEN);
    st->sender_index = load32_le(msg + 4u);
    st->in_use = 1u;

    if (peer_static_pub_out) {
        memcpy(peer_static_pub_out, st->peer_static_pub, NOISE_KEY_LEN);
    }
    if (sender_index_out) *sender_index_out = st->sender_index;
    return 0;
}

void noise_create_response(noise_responder_t* st,
                           void (*rng)(u8* out, u32 len),
                           u32 local_index,
                           u8 out_msg[WG_MSG_RESPONSE_SIZE],
                           u8 send_key[NOISE_KEY_LEN],
                           u8 recv_key[NOISE_KEY_LEN]) {
    u8 eph_priv[NOISE_KEY_LEN];
    u8 ss[NOISE_KEY_LEN];
    u8 t0[NOISE_KEY_LEN], t1[NOISE_KEY_LEN], key[NOISE_KEY_LEN];
    memset(out_msg, 0, WG_MSG_RESPONSE_SIZE);
    out_msg[0] = 2u;
    store32_le(out_msg + 4u, local_index);
    store32_le(out_msg + 8u, st->sender_index);

    rng(eph_priv, NOISE_KEY_LEN);
    noise_clamp_key(eph_priv);
    noise_x25519_pub(out_msg + HS2_OFF_EPHEMERAL, eph_priv);

    mix_hash(st->hash_state, out_msg + HS2_OFF_EPHEMERAL, NOISE_KEY_LEN);
    mix_key(st->chain_key, out_msg + HS2_OFF_EPHEMERAL, NOISE_KEY_LEN);

    /* ee */
    noise_x25519_shared(ss, eph_priv, st->peer_eph_pub);
    mix_key(st->chain_key, ss, NOISE_KEY_LEN);
    /* er: responder ephemeral x initiator static */
    noise_x25519_shared(ss, eph_priv, st->peer_static_pub);
    mix_key(st->chain_key, ss, NOISE_KEY_LEN);

    noise_kdf3(t0, t1, key, st->chain_key, st->psk, NOISE_KEY_LEN);
    memcpy(st->chain_key, t0, NOISE_KEY_LEN);
    mix_hash(st->hash_state, t1, NOISE_KEY_LEN);


    noise_aead_seal(key, 0u, st->hash_state, NOISE_KEY_LEN,
                    out_msg + HS2_OFF_EMPTY, 0u, out_msg + HS2_OFF_EMPTY);
    mix_hash(st->hash_state, out_msg + HS2_OFF_EMPTY, NOISE_TAG_LEN);

    noise_mac1(out_msg + HS2_OFF_MAC1, st->peer_static_pub, out_msg, HS2_MAC_LEN);

    /* responder view: first KDF output is receive direction */
    noise_kdf2(recv_key, send_key, st->chain_key, 0, 0u);
    memset(eph_priv, 0, NOISE_KEY_LEN);
}
