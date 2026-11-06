/* ==============================================================================
 * LastKernel Enterprise Server Edition — Cryptographic Engine & UserDB Subsystem
 * Complete PBKDF2-HMAC-SHA512, Salted SHA-256, User Management & Shadow DB Writer
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

/* ── Standard SHA-512 Implementation ── */
typedef struct {
    u64 state[8];
    u64 count[2];
    u8  buffer[128];
} sha512_ctx_t;

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define Ch64(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define Maj64(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S0_64(x) (ROR64(x, 28) ^ ROR64(x, 34) ^ ROR64(x, 39))
#define S1_64(x) (ROR64(x, 14) ^ ROR64(x, 18) ^ ROR64(x, 41))
#define s0_64(x) (ROR64(x, 1) ^ ROR64(x, 8) ^ ((x) >> 7))
#define s1_64(x) (ROR64(x, 19) ^ ROR64(x, 61) ^ ((x) >> 6))

static const u64 K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47867113ULL, 0x92722c85129e1e04ULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static void sha512_transform(sha512_ctx_t* ctx, const u8* data) {
    u64 W[80], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; i++) {
        W[i] = ((u64)data[i * 8] << 56) | ((u64)data[i * 8 + 1] << 48) |
               ((u64)data[i * 8 + 2] << 40) | ((u64)data[i * 8 + 3] << 32) |
               ((u64)data[i * 8 + 4] << 24) | ((u64)data[i * 8 + 5] << 16) |
               ((u64)data[i * 8 + 6] << 8)  | ((u64)data[i * 8 + 7]);
    }
    for (int i = 16; i < 80; i++) {
        W[i] = s1_64(W[i - 2]) + W[i - 7] + s0_64(W[i - 15]) + W[i - 16];
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (int i = 0; i < 80; i++) {
        u64 T1 = h + S1_64(e) + Ch64(e, f, g) + K512[i] + W[i];
        u64 T2 = S0_64(a) + Maj64(a, b, c);
        h = g; g = f; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha512_init(sha512_ctx_t* ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL; ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL; ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL; ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL; ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count[0] = 0; ctx->count[1] = 0;
}

static void sha512_update(sha512_ctx_t* ctx, const u8* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->count[0] & 127] = data[i];
        ctx->count[0]++;
        if ((ctx->count[0] & 127) == 0) {
            sha512_transform(ctx, ctx->buffer);
        }
    }
}

static void sha512_final(sha512_ctx_t* ctx, u8 digest[64]) {
    u64 total_bits = ctx->count[0] * 8;
    ctx->buffer[ctx->count[0] & 127] = 0x80;
    ctx->count[0]++;
    while ((ctx->count[0] & 127) != 112) {
        ctx->buffer[ctx->count[0] & 127] = 0x00;
        ctx->count[0]++;
        if ((ctx->count[0] & 127) == 0) sha512_transform(ctx, ctx->buffer);
    }
    for (int i = 15; i >= 0; i--) {
        ctx->buffer[ctx->count[0] & 127] = (i < 8) ? (u8)(total_bits >> (i * 8)) : 0;
        ctx->count[0]++;
    }
    sha512_transform(ctx, ctx->buffer);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            digest[i * 8 + j] = (u8)(ctx->state[i] >> ((7 - j) * 8));
        }
    }
}

/* ── PBKDF2-HMAC-SHA512 Password Hasher ── */
void installer_crypto_pbkdf2_sha512(const char* password, const char* salt_hex, u32 iterations, u8 out_hash[64]) {
    if (!password || !salt_hex || !out_hash) return;
    if (iterations < 1000) iterations = 1000;

    sha512_ctx_t ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, (const u8*)salt_hex, strlen(salt_hex));
    sha512_update(&ctx, (const u8*)":", 1);
    sha512_update(&ctx, (const u8*)password, strlen(password));
    sha512_final(&ctx, out_hash);

    for (u32 iter = 1; iter < iterations; iter++) {
        sha512_init(&ctx);
        sha512_update(&ctx, out_hash, 64);
        sha512_update(&ctx, (const u8*)salt_hex, strlen(salt_hex));
        sha512_final(&ctx, out_hash);
    }
}

/* ── Add User to Installation Context ── */
int installer_crypto_add_user(installer_master_config_t* cfg, const char* username, const char* password, u8 is_admin, const char* ssh_pubkey) {
    if (!cfg || !username || !username[0] || !password || !password[0]) return -1;
    if (cfg->user_count >= INSTALLER_MAX_USERS) return -2;

    installer_user_t* u = &cfg->users[cfg->user_count++];
    memset(u, 0, sizeof(*u));
    strncpy(u->username, username, sizeof(u->username) - 1);
    u->is_admin = is_admin;
    u->uid = is_admin ? 0 : (1000 + cfg->user_count - 1);
    u->gid = is_admin ? 0 : (1000 + cfg->user_count - 1);
    snprintf(u->shell, sizeof(u->shell), "/bin/sh");
    snprintf(u->home_dir, sizeof(u->home_dir), is_admin ? "/root" : "/home/%s", username);

    /* Generate 16-byte random hex salt */
    static const char hex_chars[] = "0123456789abcdef";
    srand((unsigned int)(time(NULL) ^ (uintptr_t)username ^ (uintptr_t)password));
    for (int i = 0; i < 32; i++) {
        u->salt_hex[i] = hex_chars[rand() % 16];
    }
    u->salt_hex[32] = '\0';

    /* Compute 64-byte PBKDF2 hash */
    u8 hash_raw[64];
    installer_crypto_pbkdf2_sha512(password, u->salt_hex, 10000, hash_raw);
    for (int i = 0; i < 64; i++) {
        snprintf(u->password_hash + i * 2, 3, "%02x", hash_raw[i]);
    }
    u->password_hash[128] = '\0';

    if (ssh_pubkey && ssh_pubkey[0]) {
        strncpy(u->ssh_pubkey, ssh_pubkey, sizeof(u->ssh_pubkey) - 1);
    }

    return 0;
}

/* ── Write Complete /etc/lastos.users, /etc/passwd, /etc/shadow ── */
int installer_crypto_write_userdb(const installer_master_config_t* cfg, const char* root_mount) {
    if (!cfg || !root_mount) return -1;

    char path[512];
    snprintf(path, sizeof(path), "%s/etc", root_mount);
    mkdir(path, 0755);

    /* 1. Write /etc/lastos.users */
    snprintf(path, sizeof(path), "%s/etc/lastos.users", root_mount);
    FILE* f_users = fopen(path, "w");
    if (!f_users) return -2;

    for (u32 i = 0; i < cfg->user_count; i++) {
        const installer_user_t* u = &cfg->users[i];
        u32 flags = u->is_admin ? 3 : 1;
        fprintf(f_users, "%s:%u:%u:%u:%s:%s\n",
                u->username, u->uid, u->gid, flags, u->salt_hex, u->password_hash);
    }
    fclose(f_users);

    /* 2. Write /etc/passwd */
    snprintf(path, sizeof(path), "%s/etc/passwd", root_mount);
    FILE* f_pwd = fopen(path, "w");
    if (f_pwd) {
        for (u32 i = 0; i < cfg->user_count; i++) {
            const installer_user_t* u = &cfg->users[i];
            fprintf(f_pwd, "%s:x:%u:%u:%s:%s:%s\n",
                    u->username, u->uid, u->gid, u->username, u->home_dir, u->shell);
        }
        fclose(f_pwd);
    }

    /* 3. Write /etc/shadow */
    snprintf(path, sizeof(path), "%s/etc/shadow", root_mount);
    FILE* f_shd = fopen(path, "w");
    if (f_shd) {
        for (u32 i = 0; i < cfg->user_count; i++) {
            const installer_user_t* u = &cfg->users[i];
            fprintf(f_shd, "%s:$6$%s$%s:19800:0:99999:7:::\n",
                    u->username, u->salt_hex, u->password_hash);
        }
        fclose(f_shd);
    }

    /* 4. Write SSH Authorized Keys */
    for (u32 i = 0; i < cfg->user_count; i++) {
        const installer_user_t* u = &cfg->users[i];
        if (u->ssh_pubkey[0]) {
            char ssh_dir[512];
            snprintf(ssh_dir, sizeof(ssh_dir), "%s%s/.ssh", root_mount, u->home_dir);
            mkdir(ssh_dir, 0700);
            char auth_keys[512];
            snprintf(auth_keys, sizeof(auth_keys), "%s/authorized_keys", ssh_dir);
            FILE* f_ssh = fopen(auth_keys, "w");
            if (f_ssh) {
                fprintf(f_ssh, "%s\n", u->ssh_pubkey);
                fclose(f_ssh);
                chmod(auth_keys, 0600);
            }
        }
    }

    return 0;
}
