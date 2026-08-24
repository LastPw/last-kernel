/* BLAKE2s-256 (RFC 7693), freestanding implementation for LastKernel.
 * Supports unkeyed, natively keyed, and HMAC modes. */

#include "blake2s.h"
#include "string.h"

static const u32 B2S_IV[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

static const u8 B2S_SIGMA[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15 },
    {14,10, 4, 8, 9,15,13, 6, 1,12, 0, 2,11, 7, 5, 3 },
    {11, 8,12, 0, 5, 2,15,13,10,14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1,13,12,11,14, 2, 6, 5,10, 4, 0,15, 8 },
    { 9, 0, 5, 7, 2, 4,10,15,14, 1,11,12, 6, 8, 3,13 },
    { 2,12, 6,10, 0,11, 8, 3, 4,13, 7, 5,15,14, 1, 9 },
    {12, 5, 1,15,14,13, 4,10, 0, 7, 6, 3, 9, 2, 8,11 },
    {13,11, 7,14,12, 1, 3, 9, 5, 0,15, 4, 8, 6, 2,10 },
    { 6,15,14, 9,11, 3, 0, 8,12, 2,13, 7, 1, 4,10, 5 },
    {10, 2, 8, 4, 7, 6, 1, 5,15,11, 9,14, 3,12,13, 0 }
};

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32u - (n))))

static u32 load32_le(const u8* p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

#define G(a, b, c, d, x, y)                       \
    do {                                          \
        a = a + b + x;                            \
        d = ROTR32(d ^ a, 16);                    \
        c = c + d;                                \
        b = ROTR32(b ^ c, 12);                    \
        a = a + b + y;                            \
        d = ROTR32(d ^ a, 8);                     \
        c = c + d;                                \
        b = ROTR32(b ^ c, 7);                     \
    } while (0)

static void blake2s_compress(blake2s_ctx* ctx, const u8 block[64]) {
    u32 m[16], v[16];
    for (u32 i = 0; i < 16; i++) m[i] = load32_le(block + (i * 4u));
    for (u32 i = 0; i < 8; i++) v[i] = ctx->h[i];
    for (u32 i = 0; i < 8; i++) v[8u + i] = B2S_IV[i];
    v[12] ^= ctx->t[0];
    v[13] ^= ctx->t[1];
    v[14] ^= ctx->f[0];
    v[15] ^= ctx->f[1];

    for (u32 r = 0; r < 10; r++) {
        const u8* s = B2S_SIGMA[r];
        G(v[0], v[4], v[ 8], v[12], m[s[ 0]], m[s[ 1]]);
        G(v[1], v[5], v[ 9], v[13], m[s[ 2]], m[s[ 3]]);
        G(v[2], v[6], v[10], v[14], m[s[ 4]], m[s[ 5]]);
        G(v[3], v[7], v[11], v[15], m[s[ 6]], m[s[ 7]]);
        G(v[0], v[5], v[10], v[15], m[s[ 8]], m[s[ 9]]);
        G(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
        G(v[2], v[7], v[ 8], v[13], m[s[12]], m[s[13]]);
        G(v[3], v[4], v[ 9], v[14], m[s[14]], m[s[15]]);
    }

    for (u32 i = 0; i < 8; i++) ctx->h[i] ^= v[i] ^ v[8u + i];
}

static void blake2s_init_ex(blake2s_ctx* ctx, const u8* key, u32 key_len, u32 out_len) {
    for (u32 i = 0; i < 8; i++) ctx->h[i] = B2S_IV[i];
    /* Parameter block: digest_length, fanout=1, depth=1, key_length */
    ctx->h[0] ^= 0x01010000u ^ (out_len & 0xFFu) ^ ((key_len & 0xFFu) << 8);
    ctx->t[0] = 0u;
    ctx->t[1] = 0u;
    ctx->f[0] = 0u;
    ctx->f[1] = 0u;
    ctx->buflen = 0u;
    if (key_len > 0u) {
        u8 kblock[BLAKE2S_BLOCKBYTES];
        memset(kblock, 0, sizeof(kblock));
        memcpy(kblock, key, key_len);
        blake2s_update(ctx, kblock, BLAKE2S_BLOCKBYTES);
    }
}

void blake2s_init(blake2s_ctx* ctx) {
    blake2s_init_ex(ctx, 0, 0u, BLAKE2S_OUTBYTES);
}

void blake2s_update(blake2s_ctx* ctx, const void* data, u32 len) {
    const u8* p = (const u8*)data;
    while (len > 0u) {
        u32 take;
        if (ctx->buflen == 0u && len > BLAKE2S_BLOCKBYTES) {
            take = BLAKE2S_BLOCKBYTES;
            ctx->t[0] += take;
            if (ctx->t[0] < take) ctx->t[1]++;
            blake2s_compress(ctx, p);
        } else {
            take = BLAKE2S_BLOCKBYTES - ctx->buflen;
            if (take > len) take = len;
            memcpy(ctx->buf + ctx->buflen, p, take);
            ctx->buflen += take;
            if (ctx->buflen == BLAKE2S_BLOCKBYTES && len > take) {
                ctx->t[0] += BLAKE2S_BLOCKBYTES;
                if (ctx->t[0] < BLAKE2S_BLOCKBYTES) ctx->t[1]++;
                blake2s_compress(ctx, ctx->buf);
                ctx->buflen = 0u;
            } else {
                len = take;
                break;
            }
        }
        p += take;
        len -= take;
    }
}

void blake2s_final(blake2s_ctx* ctx, u8 out[BLAKE2S_OUTBYTES]) {
    u8 full[BLAKE2S_OUTBYTES];
    ctx->t[0] += ctx->buflen;
    if (ctx->t[0] < ctx->buflen) ctx->t[1]++;
    memset(ctx->buf + ctx->buflen, 0, BLAKE2S_BLOCKBYTES - ctx->buflen);
    ctx->f[0] = 0xFFFFFFFFu;
    blake2s_compress(ctx, ctx->buf);
    for (u32 i = 0; i < 8; i++) {
        full[(i * 4u) + 0] = (u8)(ctx->h[i]);
        full[(i * 4u) + 1] = (u8)(ctx->h[i] >> 8);
        full[(i * 4u) + 2] = (u8)(ctx->h[i] >> 16);
        full[(i * 4u) + 3] = (u8)(ctx->h[i] >> 24);
    }
    memcpy(out, full, BLAKE2S_OUTBYTES);
}

void blake2s(const void* data, u32 len, u8 out[BLAKE2S_OUTBYTES]) {
    blake2s_ctx ctx;
    blake2s_init_ex(&ctx, 0, 0u, BLAKE2S_OUTBYTES);
    blake2s_update(&ctx, data, len);
    blake2s_final(&ctx, out);
}

void blake2s_keyed(const u8* key, u32 key_len, const void* data, u32 len,
                   u8 out[BLAKE2S_OUTBYTES]) {
    blake2s_ctx ctx;
    if (!key || key_len == 0u || key_len > BLAKE2S_KEYBYTES) {
        blake2s(data, len, out);
        return;
    }
    blake2s_init_ex(&ctx, key, key_len, BLAKE2S_OUTBYTES);
    blake2s_update(&ctx, data, len);
    blake2s_final(&ctx, out);
}

/* HMAC-BLAKE2s per RFC 2104 with B=64, L=32 */
void hmac_blake2s(const u8* key, u32 key_len, const void* data, u32 len,
                  u8 out[BLAKE2S_OUTBYTES]) {
    u8 kblock[BLAKE2S_BLOCKBYTES];
    u8 ipad[BLAKE2S_BLOCKBYTES];
    u8 opad[BLAKE2S_BLOCKBYTES];
    u8 khash[BLAKE2S_OUTBYTES];
    u8 inner[BLAKE2S_OUTBYTES];
    blake2s_ctx ctx;

    if (!key || key_len == 0u) {
        blake2s(data, len, out);
        return;
    }
    memset(kblock, 0, sizeof(kblock));
    if (key_len > BLAKE2S_BLOCKBYTES) {
        blake2s(key, key_len, khash);
        memcpy(kblock, khash, sizeof(khash));
    } else {
        memcpy(kblock, key, key_len);
    }
    for (u32 i = 0; i < BLAKE2S_BLOCKBYTES; i++) {
        ipad[i] = (u8)(kblock[i] ^ 0x36u);
        opad[i] = (u8)(kblock[i] ^ 0x5Cu);
    }
    blake2s_init_ex(&ctx, 0, 0u, BLAKE2S_OUTBYTES);
    blake2s_update(&ctx, ipad, BLAKE2S_BLOCKBYTES);
    blake2s_update(&ctx, data, len);
    blake2s_final(&ctx, inner);

    blake2s_init_ex(&ctx, 0, 0u, BLAKE2S_OUTBYTES);
    blake2s_update(&ctx, opad, BLAKE2S_BLOCKBYTES);
    blake2s_update(&ctx, inner, sizeof(inner));
    blake2s_final(&ctx, out);
}

void blake2s_keyed_ex(void* out, u32 out_len,
                      const u8* key, u32 key_len,
                      const void* data, u32 data_len) {
    blake2s_ctx ctx;
    u8 full[BLAKE2S_OUTBYTES];
    if (out_len == 0u || out_len > BLAKE2S_OUTBYTES) return;
    if (!key || key_len == 0u || key_len > BLAKE2S_KEYBYTES) {
        blake2s_init_ex(&ctx, 0, 0u, out_len);
    } else {
        blake2s_init_ex(&ctx, key, key_len, out_len);
    }
    blake2s_update(&ctx, data, data_len);
    blake2s_final(&ctx, full);
    memcpy(out, full, out_len);
}
