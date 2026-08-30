#ifndef SHA256_H
#define SHA256_H

#include "types.h"

#define SHA256_DIGEST_SIZE 32u
#define SHA256_BLOCK_SIZE 64u

typedef struct {
    u32 state[8];
    u64 bit_count;
    u8 block[SHA256_BLOCK_SIZE];
    u32 block_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t* ctx);
void sha256_update(sha256_ctx_t* ctx, const u8* data, u32 len);
void sha256_final(sha256_ctx_t* ctx, u8 digest[SHA256_DIGEST_SIZE]);
void sha256(const u8* data, u32 len, u8 digest[SHA256_DIGEST_SIZE]);
void hmac_sha256(const u8* key, u32 key_len, const u8* data, u32 data_len,
                 u8 digest[SHA256_DIGEST_SIZE]);
int hkdf_sha256_extract(const u8* salt, u32 salt_len, const u8* ikm, u32 ikm_len,
                        u8 prk[SHA256_DIGEST_SIZE]);
int hkdf_sha256_expand(const u8 prk[SHA256_DIGEST_SIZE], const u8* info, u32 info_len,
                       u8* out, u32 out_len);
int crypto_equal(const u8* a, const u8* b, u32 len);

#endif
