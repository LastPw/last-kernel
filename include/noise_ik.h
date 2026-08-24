#ifndef NOISE_IK_H
#define NOISE_IK_H

/* WireGuard Noise_IKpsk2 handshake core (whitepaper-exact sequence).
 * Pure computation, host-testable. Curve25519 keys are RFC 7748
 * little-endian wire encoding. All multi-byte integers little-endian. */

#include "types.h"

#define NOISE_KEY_LEN       32u
#define NOISE_TAG_LEN       16u
#define NOISE_MAC1_LEN      16u
#define NOISE_TIMESTAMP_LEN 12u

#define WG_CONSTRUCTION "Noise_IKpsk2_V25519ChaChaPoly_BLAKE2s"
#define WG_IDENTIFIER   "WireGuard v1 zx2c4 Jason@zx2c4.com"
#define WG_LABEL_MAC1   "mac1----"

#define WG_MSG_INITIATION_SIZE 148u
#define WG_MSG_RESPONSE_SIZE   92u

void noise_clamp_key(u8 priv_le[NOISE_KEY_LEN]);
void noise_x25519_pub(u8 out_pub[NOISE_KEY_LEN], const u8 priv_le[NOISE_KEY_LEN]);
void noise_x25519_shared(u8 out_shared[NOISE_KEY_LEN],
                         const u8 priv_le[NOISE_KEY_LEN],
                         const u8 peer_pub_le[NOISE_KEY_LEN]);

/* KDF_n: prk = HMAC-BLAKE2s(key, m); T_i chained per RFC 5869 style */
void noise_kdf1(u8 t0[NOISE_KEY_LEN], const u8 key[NOISE_KEY_LEN],
                const void* m, u32 m_len);
void noise_kdf2(u8 t0[NOISE_KEY_LEN], u8 t1[NOISE_KEY_LEN],
                const u8 key[NOISE_KEY_LEN], const void* m, u32 m_len);
void noise_kdf3(u8 t0[NOISE_KEY_LEN], u8 t1[NOISE_KEY_LEN], u8 t2[NOISE_KEY_LEN],
                const u8 key[NOISE_KEY_LEN], const void* m, u32 m_len);

/* ChaCha20-Poly1305 with WG nonce layout zeros[4] || counter_le64 */
void noise_aead_seal(const u8 key[NOISE_KEY_LEN], u64 counter,
                     const void* ad, u32 ad_len,
                     void* data, u32 data_len, u8 tag[NOISE_TAG_LEN]);
int  noise_aead_open(const u8 key[NOISE_KEY_LEN], u64 counter,
                     const void* ad, u32 ad_len,
                     void* data, u32 data_len, const u8 tag[NOISE_TAG_LEN]);

/* MAC1: keyed BLAKE2s-128 over the pre-MAC portion of a handshake msg */
void noise_mac1(u8 mac[NOISE_MAC1_LEN], const u8 peer_static_pub[NOISE_KEY_LEN],
                const void* msg, u32 mac_len);
int  noise_mac1_verify(const u8 expected[NOISE_MAC1_LEN],
                       const u8 peer_static_pub[NOISE_KEY_LEN],
                       const void* msg, u32 mac_len);

/* ---------------- initiator ---------------- */

typedef struct {
    u8  chaining_key[NOISE_KEY_LEN];
    u8  transcript_hash[NOISE_KEY_LEN];
    u8  eph_priv[NOISE_KEY_LEN];
    u8  my_static_priv[NOISE_KEY_LEN];
    u8  peer_static_pub[NOISE_KEY_LEN];
    u8  psk[NOISE_KEY_LEN];
    u32 sender_index;
    u8  in_use;
} noise_initiator_t;

void noise_create_initiation(noise_initiator_t* st,
                             const u8 my_static_priv[NOISE_KEY_LEN],
                             const u8 my_static_pub[NOISE_KEY_LEN],
                             const u8 peer_static_pub[NOISE_KEY_LEN],
                             const u8 psk_or_null[NOISE_KEY_LEN],
                             const u8 timestamp[NOISE_TIMESTAMP_LEN],
                             void (*rng)(u8* out, u32 len),
                             u8 out_msg[WG_MSG_INITIATION_SIZE]);

int noise_consume_response(noise_initiator_t* st,
                           const u8 msg[WG_MSG_RESPONSE_SIZE],
                           u8 send_key[NOISE_KEY_LEN],
                           u8 recv_key[NOISE_KEY_LEN],
                           u32* remote_index_out);

/* ---------------- responder ---------------- */

typedef struct {
    u8  chain_key[NOISE_KEY_LEN];
    u8  hash_state[NOISE_KEY_LEN];
    u8  peer_static_pub[NOISE_KEY_LEN];
    u8  peer_eph_pub[NOISE_KEY_LEN];
    u8  psk[NOISE_KEY_LEN];
    u32 sender_index;      /* initiator's session index */
    u8  in_use;
} noise_responder_t;

int noise_accept_initiation(noise_responder_t* st,
                            const u8 my_static_priv[NOISE_KEY_LEN],
                            const u8 my_static_pub[NOISE_KEY_LEN],
                            const u8 psk_or_null[NOISE_KEY_LEN],
                            const u8 msg[WG_MSG_INITIATION_SIZE],
                            u8 peer_static_pub_out[NOISE_KEY_LEN],
                            u8 timestamp_out[NOISE_TIMESTAMP_LEN],
                            u32* sender_index_out);

void noise_create_response(noise_responder_t* st,
                           void (*rng)(u8* out, u32 len),
                           u32 local_index,
                           u8 out_msg[WG_MSG_RESPONSE_SIZE],
                           u8 send_key[NOISE_KEY_LEN],
                           u8 recv_key[NOISE_KEY_LEN]);

#endif /* NOISE_IK_H */
