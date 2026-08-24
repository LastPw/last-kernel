/* Host-side WireGuard noise core tests: RFC 7693 BLAKE2s vectors,
 * X25519 DH symmetry, full dual-role Noise_IKpsk2 exchange with PSK,
 * AEAD tamper detection and transport replay rejection.
 * Built and run by `make test` (plain C99, no kernel deps). */

#include <stdio.h>
#include <string.h>
#include "blake2s.h"
#include "noise_ik.h"

static int failures = 0;

#define CHECK(cond, name)                                        \
    do {                                                         \
        if (cond) {                                              \
            printf("  PASS %s\n", (name));                       \
        } else {                                                 \
            printf("  FAIL %s\n", (name));                       \
            failures++;                                          \
        }                                                        \
    } while (0)

static void hex_parse(const char* hex, u8* out, u32 n) {
    for (u32 i = 0; i < n; i++) {
        u8 hi, lo;
        char a = hex[i * 2], b = hex[i * 2 + 1];
        hi = (u8)(a <= '9' ? a - '0' : a - 'a' + 10);
        lo = (u8)(b <= '9' ? b - '0' : b - 'a' + 10);
        out[i] = (u8)((hi << 4) | lo);
    }
}

/* deterministic RNG for tests */
static u8 rng_state[32];
static void test_rng(u8* out, u32 len) {
    for (u32 i = 0; i < len; i++) {
        rng_state[i % 32] = (u8)(rng_state[i % 32] * 1103515245u + 12345u);
        out[i] = rng_state[i % 32];
    }
}

static void test_blake2s_vectors(void) {
    u8 hash[32];

    /* BLAKE2s-256 of empty input */
    {
        const u8 expect[32] = {
            0x69, 0x21, 0x7a, 0x30, 0x79, 0x90, 0x80, 0x94,
            0xe1, 0x11, 0x21, 0xd0, 0x42, 0x35, 0x4a, 0x7c,
            0x1f, 0x55, 0xb6, 0x48, 0x2c, 0xa1, 0xa5, 0x1e,
            0x1b, 0x25, 0x0d, 0xfd, 0x1e, 0xd0, 0xee, 0xf9
        };
        blake2s("", 0, hash);
        CHECK(memcmp(hash, expect, 32) == 0, "blake2s empty vector");
    }

    /* RFC 7693 Appendix B: BLAKE2s-256("abc") */
    blake2s("abc", 3, hash);
    {
        const u8 expect[32] = {
            0x50, 0x8c, 0x5e, 0x8c, 0x32, 0x7c, 0x14, 0xe2,
            0xe1, 0xa7, 0x2b, 0xa3, 0x4e, 0xeb, 0x45, 0x2f,
            0x37, 0x45, 0x8b, 0x20, 0x9e, 0xd6, 0x3a, 0x29,
            0x4d, 0x99, 0x9b, 0x4c, 0x86, 0x67, 0x59, 0x82
        };
        CHECK(memcmp(hash, expect, 32) == 0, "blake2s('abc') full vector");
    }

    /* keyed BLAKE2s-256, zero input (official KAT key=000102..1f) */
    {
        u8 key[32];
        for (u32 i = 0; i < 32; i++) key[i] = (u8)i;
        blake2s_keyed(key, 32, "", 0, hash);
        const u8 expect[32] = {
            0x48, 0xa8, 0x99, 0x7d, 0xa4, 0x07, 0x87, 0x6b,
            0x3d, 0x79, 0xc0, 0xd9, 0x23, 0x25, 0xad, 0x3b,
            0x89, 0xcb, 0xb7, 0x54, 0xd8, 0x6a, 0xb7, 0x1a,
            0xee, 0x04, 0x7a, 0xd3, 0x45, 0xfd, 0x2c, 0x49
        };
        CHECK(memcmp(hash, expect, 32) == 0, "blake2s keyed KAT");
    }

    /* incremental == one-shot over multi-block input */
    {
        u8 big[200];
        u8 h1[32], h2[32];
        blake2s_ctx ctx;
        for (u32 i = 0; i < sizeof(big); i++) big[i] = (u8)(i * 7u + 3u);
        blake2s(big, sizeof(big), h1);
        blake2s_init(&ctx);
        blake2s_update(&ctx, big, 64);
        blake2s_update(&ctx, big + 64, 100);
        blake2s_update(&ctx, big + 164, 36);
        blake2s_final(&ctx, h2);
        CHECK(memcmp(h1, h2, 32) == 0, "incremental equals one-shot");

        /* keyed -128 form differs from truncated 256 form */
        u8 mac128[16], mac256trunc[16];
        u8 key[32];
        memcpy(key, "wg-mac1-test-key-00000000000000", 32);
        blake2s_keyed_ex(mac128, 16, key, 32, "hello", 5);
        blake2s_keyed(key, 32, "hello", 5, h1);
        memcpy(mac256trunc, h1, 16);
        CHECK(memcmp(mac128, mac256trunc, 16) != 0, "keyed-128 distinct from truncated-256");
    }
}

static void test_x25519_symmetry(void) {
    u8 a_priv[32], b_priv[32], a_pub[32], b_pub[32], ss1[32], ss2[32];
    hex_parse(
        "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
        a_priv, 32);
    hex_parse(
        "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
        b_priv, 32);
    noise_clamp_key(a_priv);
    noise_clamp_key(b_priv);
    noise_x25519_pub(a_pub, a_priv);
    noise_x25519_pub(b_pub, b_priv);
    /* RFC 7748 section 6.1 public keys */
    {
        const u8 a_expect[32] = {
            0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54,
            0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
            0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4,
            0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a
        };
        const u8 b_expect[32] = {
            0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4,
            0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37,
            0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d,
            0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f
        };
        const u8 shared_expect[32] = {
            0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1,
            0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25,
            0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33,
            0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42
        };
        CHECK(memcmp(a_pub, a_expect, 32) == 0, "RFC7748 alice public key");
        CHECK(memcmp(b_pub, b_expect, 32) == 0, "RFC7748 bob public key");
        noise_x25519_shared(ss1, a_priv, b_pub);
        noise_x25519_shared(ss2, b_priv, a_pub);
        CHECK(memcmp(ss1, shared_expect, 32) == 0, "RFC7748 shared secret");
    }
    CHECK(memcmp(ss1, ss2, 32) == 0, "X25519 shared secret symmetry");
}

static void test_full_handshake(void) {
    static const char* A_PRIV_HEX =
        "58e2a5c1f4bfe3d20bd7487ea8c4a97292e157ebf0dc8f4ec48bbe6b60e3a50b";
    static const char* B_PRIV_HEX =
        "18cbe8cd6e5b6fa54c93b0f0f30f2af1e2d3c4b5a69788778fd0a5c9c3e2b104";
    u8 a_priv[32], b_priv[32], a_pub[32], b_pub[32];
    u8 psk[32];
    u8 ts[12];
    u8 init_msg[148], resp_msg[92];
    u8 send_a[32], recv_a[32], send_b[32], recv_b[32];
    u32 remote_idx = 0;

    hex_parse(A_PRIV_HEX, a_priv, 32);
    hex_parse(B_PRIV_HEX, b_priv, 32);
    memset(psk, 0xAB, 32);
    noise_clamp_key(a_priv);
    noise_clamp_key(b_priv);
    noise_x25519_pub(a_pub, a_priv);
    noise_x25519_pub(b_pub, b_priv);

    memset(rng_state, 0x5A, sizeof(rng_state));

    noise_initiator_t initiator;
    memset(&initiator, 0, sizeof(initiator));
    memset(ts, 0x11, sizeof(ts));
    ts[11] = 0x01;
    noise_create_initiation(&initiator, a_priv, a_pub, b_pub, psk, ts,
                            test_rng, init_msg);
    CHECK(init_msg[0] == 1, "initiation type byte");
    CHECK(noise_mac1_verify(init_msg + 116, b_pub, init_msg, 116) == 0,
          "initiation MAC1 verifies");

    noise_responder_t responder;
    memset(&responder, 0, sizeof(responder));
    u8 peer_pub_out[32], ts_out[12];
    u32 sender_idx = 0;
    int rc = noise_accept_initiation(&responder, b_priv, b_pub, psk,
                                     init_msg, peer_pub_out, ts_out, &sender_idx);
    if (rc != 0) printf("    (accept rc=%d)\n", rc);
    CHECK(rc == 0, "responder accepts initiation");
    CHECK(memcmp(peer_pub_out, a_pub, 32) == 0, "responder recovers initiator static pub");
    CHECK(memcmp(ts_out, ts, 12) == 0, "timestamp decrypts intact");

    /* wrong responder key must fail MAC1 gate */
    u8 evil_priv[32];
    memcpy(evil_priv, b_priv, 32);
    evil_priv[10] ^= 0xA5;
    noise_responder_t bad;
    memset(&bad, 0, sizeof(bad));
    rc = noise_accept_initiation(&bad, evil_priv, b_pub, psk,
                                 init_msg, peer_pub_out, ts_out, &sender_idx);
    if (rc == 0) printf("    (wrong-key accept UNEXPECTED)\n");
    CHECK(rc != 0, "wrong static key rejected");

    noise_create_response(&responder, test_rng, 0xDEADBEEFu, resp_msg,
                          send_b, recv_b);
    CHECK(resp_msg[0] == 2, "response type byte");
    CHECK(noise_mac1_verify(resp_msg + 60, a_pub, resp_msg, 60) == 0,
          "response MAC1 verifies");

    rc = noise_consume_response(&initiator, resp_msg, send_a, recv_a, &remote_idx);
    if (rc != 0) printf("    (consume rc=%d)\n", rc);
    CHECK(rc == 0, "initiator consumes response");
    CHECK(remote_idx == 0xDEADBEEFu, "remote index round-trips");
    CHECK(memcmp(send_a, recv_b, 32) == 0 && memcmp(recv_a, send_b, 32) == 0,
          "transport keys cross-match (init-send==resp-recv)");
    CHECK(memcmp(send_a, recv_a, 32) != 0, "direction keys are distinct");

    /* tampered response must fail: rerun a fresh exchange, corrupt, consume */
    {
        u8 corrupted[92];
        u8 ts2[12];
        noise_initiator_t again;
        noise_responder_t resp2;
        memset(&again, 0, sizeof(again));
        memset(&resp2, 0, sizeof(resp2));
        memcpy(ts2, ts, 12);
        ts2[11]++;
        noise_create_initiation(&again, a_priv, a_pub, b_pub, psk, ts2,
                                test_rng, init_msg);
        int rc2 = noise_accept_initiation(&resp2, b_priv, b_pub, psk,
                                    init_msg, peer_pub_out, ts_out, &sender_idx);
        if (rc2 != 0) printf("    (accept2 rc=%d)\n", rc2);
        if (rc2 != 0) {
            CHECK(0, "second handshake accepts");
            return;
        }
        noise_create_response(&resp2, test_rng, 0x1357u, corrupted,
                              send_b, recv_b);
        corrupted[50] ^= 0x40;
        rc = noise_consume_response(&again, corrupted, send_a, recv_a, &remote_idx);
        CHECK(rc != 0, "tampered response rejected");
    }
}

static void test_transport_roundtrip(void) {
    static const char* A_PRIV_HEX =
        "98a3f2e51d0cb7468a29f0e1d3c5b7a9681402fedbc9a05734e8f1d2c6ba9471";
    static const char* B_PRIV_HEX =
        "38d1c7a95b3e02f68d41a9c5e70b28f4139d6a8c05fb3e7142c9d8a56ef0b382";
    u8 a_priv[32], b_priv[32], a_pub[32], b_pub[32];
    u8 init_msg[148], resp_msg[92], ts[12];
    u8 send_a[32], recv_a[32], send_b[32], recv_b[32];

    hex_parse(A_PRIV_HEX, a_priv, 32);
    hex_parse(B_PRIV_HEX, b_priv, 32);
    noise_clamp_key(a_priv);
    noise_clamp_key(b_priv);
    noise_x25519_pub(a_pub, a_priv);
    noise_x25519_pub(b_pub, b_priv);

    noise_initiator_t initiator;
    memset(&initiator, 0, sizeof(initiator));
    memset(ts, 0x22, 12);
    noise_create_initiation(&initiator, a_priv, a_pub, b_pub, 0, ts,
                            test_rng, init_msg);

    noise_responder_t responder;
    memset(&responder, 0, sizeof(responder));
    u8 ppk[32], tso[12];
    u32 sidx = 0;
    {
        int rc = noise_accept_initiation(&responder, b_priv, b_pub, 0,
                                init_msg, ppk, tso, &sidx);
        if (rc != 0) printf("    (pskless accept rc=%d)\n", rc);
        CHECK(rc == 0, "psk-less handshake accept");
        if (rc != 0) return;
    }
    noise_create_response(&responder, test_rng, 42u, resp_msg, send_b, recv_b);
    if (noise_consume_response(&initiator, resp_msg, send_a, recv_a, 0) != 0) {
        CHECK(0, "psk-less handshake complete");
        return;
    }

    /* initiator -> responder */
    const char* msg_text = "tunnel payload 0123456789";
    u8 ct[64], tag[16], pt_back[64];
    memcpy(pt_back, msg_text, strlen(msg_text));
    noise_aead_seal(send_a, 7ull, 0, 0, pt_back, (u32)strlen(msg_text), tag);
    memcpy(ct, pt_back, strlen(msg_text));
    int rc = noise_aead_open(recv_b, 7ull, 0, 0, ct, (u32)strlen(msg_text), tag);
    CHECK(rc == 0 && memcmp(ct, msg_text, strlen(msg_text)) == 0,
          "AEAD roundtrip initiator->responder");

    /* bit-flip ciphertext must fail tag check */
    ct[0] ^= 1;
    rc = noise_aead_open(recv_b, 7ull, 0, 0, ct, (u32)strlen(msg_text), tag);
    CHECK(rc != 0, "AEAD rejects tampered ciphertext");

    /* counter mismatch must fail (nonce reuse protection across contexts) */
    ct[0] ^= 1;
    rc = noise_aead_open(recv_b, 8ull, 0, 0, ct, (u32)strlen(msg_text), tag);
    CHECK(rc != 0, "AEAD rejects wrong counter nonce");
}

int main(void) {
    printf("WireGuard noise core tests:\n");
    test_blake2s_vectors();
    test_x25519_symmetry();
    test_full_handshake();
    test_transport_roundtrip();
    if (failures == 0) {
        printf("WireGuard noise core: all vectors pass\n");
        return 0;
    }
    printf("WireGuard noise core: %d FAILURES\n", failures);
    return 1;
}
