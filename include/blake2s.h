#ifndef BLAKE2S_H
#define BLAKE2S_H

#include "types.h"

#define BLAKE2S_BLOCKBYTES 64u
#define BLAKE2S_OUTBYTES   32u
#define BLAKE2S_KEYBYTES   32u

typedef struct {
    u32 h[8];
    u32 t[2];
    u32 f[2];
    u8  buf[BLAKE2S_BLOCKBYTES];
    u32 buflen;
} blake2s_ctx;

/* Incremental API */
void blake2s_init(blake2s_ctx* ctx);
void blake2s_update(blake2s_ctx* ctx, const void* data, u32 len);
void blake2s_final(blake2s_ctx* ctx, u8 out[BLAKE2S_OUTBYTES]);

/* General keyed/unkeyed form with selectable digest length (1..32).
 * Required for WireGuard MAC1 (keyed BLAKE2s-128). */
void blake2s_keyed_ex(void* out, u32 out_len,
                      const u8* key, u32 key_len,
                      const void* data, u32 data_len);

/* Unkeyed hash: out = BLAKE2s-256(data) */
void blake2s(const void* data, u32 len, u8 out[BLAKE2S_OUTBYTES]);

/* Keyed hash (native BLAKE2 keying, up to 32-byte key): MAC construction */
void blake2s_keyed(const u8* key, u32 key_len, const void* data, u32 len,
                   u8 out[BLAKE2S_OUTBYTES]);

/* RFC 2104 HMAC instantiated with BLAKE2s-256 (64-byte block) */
void hmac_blake2s(const u8* key, u32 key_len, const void* data, u32 len,
                  u8 out[BLAKE2S_OUTBYTES]);

#endif /* BLAKE2S_H */
