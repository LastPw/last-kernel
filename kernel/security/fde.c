#include "fde.h"
#include "disk.h"
#include "sha256.h"
#include "entropy.h"
#include "string.h"
#include "serial.h"

/* ==================== Static State ==================== */

static fde_state_t g_fde;

/* Static buffers (reuse sector buffer from disk layer) */
static u8 g_fde_header_buf[FDE_SECTOR_SIZE];
static u8 g_fde_work_buf[FDE_SECTOR_SIZE];
static u8 g_fde_key_buf[FDE_KEY_SIZE];

/* ==================== AES-128 Implementation ==================== */

/* AES S-Box */
static const u8 sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* AES inverse S-Box */
static const u8 inv_sbox[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

/* Round constants */
static const u32 rcon[11] = {
    0x00000000, 0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000, 0x1B000000, 0x36000000
};

/* GF(2^8) multiply helper */
static u8 xtime(u8 a) {
    return (u8)((a << 1) ^ (((a >> 7) & 1) * 0x1B));
}

/* SubWord: apply S-Box to each byte of a 32-bit word */
static u32 sub_word(u32 w) {
    return ((u32)sbox[(u8)(w >> 24)] << 24) |
           ((u32)sbox[(u8)(w >> 16)] << 16) |
           ((u32)sbox[(u8)(w >> 8)]  << 8)  |
           (u32)sbox[(u8)w];
}

/* AES-256 key expansion (14 rounds, 60 words in round key) */
static void aes_key_expand(const u8 key[32], u32 rk[60]) {
    u32 temp;
    u32 i = 0;

    /* Copy first 8 words directly from 256-bit key */
    for (i = 0; i < 8; i++) {
        rk[i] = ((u32)key[4*i] << 24) | ((u32)key[4*i+1] << 16) |
                ((u32)key[4*i+2] << 8)  | (u32)key[4*i+3];
    }

    for (i = 8; i < 60; i++) {
        temp = rk[i-1];
        if (i % 8 == 0) {
            temp = sub_word((temp << 8) | (temp >> 24)) ^ rcon[i/8];
        } else if (i % 8 == 4) {
            temp = sub_word(temp);
        }
        rk[i] = rk[i-8] ^ temp;
    }
}

/* AddRoundKey: XOR state with round key */
static void add_round_key(u8 state[16], const u32 rk[4]) {
    for (u32 i = 0; i < 4; i++) {
        u32 k = rk[i];
        state[4*i]   ^= (u8)(k >> 24);
        state[4*i+1] ^= (u8)(k >> 16);
        state[4*i+2] ^= (u8)(k >> 8);
        state[4*i+3] ^= (u8)k;
    }
}

/* SubBytes: apply S-Box substitution */
static void sub_bytes(u8 state[16]) {
    for (u32 i = 0; i < 16; i++) state[i] = sbox[state[i]];
}

/* InvSubBytes: apply inverse S-Box */
static void inv_sub_bytes(u8 state[16]) {
    for (u32 i = 0; i < 16; i++) state[i] = inv_sbox[state[i]];
}

/* ShiftRows */
static void shift_rows(u8 state[16]) {
    u8 tmp;
    /* Row 1: shift left by 1 */
    tmp = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = tmp;
    /* Row 2: shift left by 2 */
    tmp = state[2]; state[2] = state[10]; state[10] = tmp;
    tmp = state[6]; state[6] = state[14]; state[14] = tmp;
    /* Row 3: shift left by 3 (right by 1) */
    tmp = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = tmp;
}

/* InvShiftRows */
static void inv_shift_rows(u8 state[16]) {
    u8 tmp;
    /* Row 1: shift right by 1 */
    tmp = state[13]; state[13] = state[9]; state[9] = state[5]; state[5] = state[1]; state[1] = tmp;
    /* Row 2: shift right by 2 */
    tmp = state[2]; state[2] = state[10]; state[10] = tmp;
    tmp = state[6]; state[6] = state[14]; state[14] = tmp;
    /* Row 3: shift right by 3 (left by 1) */
    tmp = state[3]; state[3] = state[7]; state[7] = state[11]; state[11] = state[15]; state[15] = tmp;
}

/* MixColumns */
static void mix_columns(u8 state[16]) {
    for (u32 i = 0; i < 4; i++) {
        u8* col = state + i * 4;
        u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        u8 t = a0 ^ a1 ^ a2 ^ a3;
        col[0] ^= t ^ xtime(a0 ^ a1);
        col[1] ^= t ^ xtime(a1 ^ a2);
        col[2] ^= t ^ xtime(a2 ^ a3);
        col[3] ^= t ^ xtime(a3 ^ a0);
    }
}

/* InvMixColumns */
static void inv_mix_columns(u8 state[16]) {
    for (u32 i = 0; i < 4; i++) {
        u8* col = state + i * 4;
        u8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
        u8 u = xtime(xtime(a0 ^ a2));
        u8 v = xtime(xtime(a1 ^ a3));
        u8 b0 = (u8)(a0 ^ u), b1 = (u8)(a1 ^ v);
        u8 b2 = (u8)(a2 ^ u), b3 = (u8)(a3 ^ v);
        u8 t = (u8)(b0 ^ b1 ^ b2 ^ b3);
        col[0] = (u8)(b0 ^ t ^ xtime((u8)(b0 ^ b1)));
        col[1] = (u8)(b1 ^ t ^ xtime((u8)(b1 ^ b2)));
        col[2] = (u8)(b2 ^ t ^ xtime((u8)(b2 ^ b3)));
        col[3] = (u8)(b3 ^ t ^ xtime((u8)(b3 ^ b0)));
    }
}

/* Encrypt one 16-byte block with AES-256 (14 rounds) */
static void aes_encrypt_block(const u8 key[32], const u8 in[16], u8 out[16]) {
    u32 rk[60];
    u8 state[16];
    memcpy(state, in, 16);

    aes_key_expand(key, rk);
    add_round_key(state, rk);

    for (u32 round = 1; round < 14; round++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, rk + round * 4);
    }

    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, rk + 56);

    memcpy(out, state, 16);
}

/* Decrypt one 16-byte block with AES-256 (14 rounds) */
static void aes_decrypt_block(const u8 key[32], const u8 in[16], u8 out[16]) {
    u32 rk[60];
    u8 state[16];
    memcpy(state, in, 16);

    aes_key_expand(key, rk);
    add_round_key(state, rk + 56);

    for (u32 round = 1; round < 14; round++) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, rk + (14 - round) * 4);
        inv_mix_columns(state);
    }

    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, rk);

    memcpy(out, state, 16);
}

/* ==================== GF(2^128) for XTS Tweak ==================== */

static void gf128_mulx(u8 x[16]) {
    u8 carry = 0;
    u8 carry_out = (x[15] >> 7) & 1;

    for (i32 i = 0; i < 16; i++) {
        u8 new_carry = (x[i] >> 7) & 1;
        x[i] = (u8)((x[i] << 1) | carry);
        carry = new_carry;
    }

    if (carry_out) {
        x[0] ^= 0x87;
    }
}

/* Encrypt a tweak value: tweak = AES_k2(sector_number || 0,...,0) */
static void aes_xts_tweak(const u8 key2[32], u64 sector_num, u8 tweak[16]) {
    memset(tweak, 0, 16);
    tweak[0] = (u8)(sector_num & 0xFF);
    tweak[1] = (u8)((sector_num >> 8) & 0xFF);
    tweak[2] = (u8)((sector_num >> 16) & 0xFF);
    tweak[3] = (u8)((sector_num >> 24) & 0xFF);
    tweak[4] = (u8)((sector_num >> 32) & 0xFF);
    tweak[5] = (u8)((sector_num >> 40) & 0xFF);
    tweak[6] = (u8)((sector_num >> 48) & 0xFF);
    tweak[7] = (u8)((sector_num >> 56) & 0xFF);
    aes_encrypt_block(key2, tweak, tweak);
}

/* ==================== AES-XTS Sector Encrypt/Decrypt ==================== */

/* Encrypt one 512-byte sector using AES-256-XTS */
static void aes_xts_encrypt_sector(const u8 key1[32], const u8 key2[32],
                                    u64 sector_num, u8 sector[512]) {
    u8 tweak[16];
    u8 block[16];
    aes_xts_tweak(key2, sector_num, tweak);

    for (u32 i = 0; i < FDE_BLOCKS_PER_SECTOR; i++) {
        u8* blk = sector + i * FDE_BLOCK_SIZE;

        for (u32 j = 0; j < 16; j++) block[j] = blk[j] ^ tweak[j];
        aes_encrypt_block(key1, block, block);
        for (u32 j = 0; j < 16; j++) blk[j] = block[j] ^ tweak[j];
        gf128_mulx(tweak);
    }
}

/* Decrypt one 512-byte sector using AES-256-XTS */
static void aes_xts_decrypt_sector(const u8 key1[32], const u8 key2[32],
                                    u64 sector_num, u8 sector[512]) {
    u8 tweak[16];
    u8 block[16];
    aes_xts_tweak(key2, sector_num, tweak);

    for (u32 i = 0; i < FDE_BLOCKS_PER_SECTOR; i++) {
        u8* blk = sector + i * FDE_BLOCK_SIZE;

        /* C' = C ⊕ Tweak */
        for (u32 j = 0; j < 16; j++) block[j] = blk[j] ^ tweak[j];

        /* P' = AES_k1^{-1}(C') */
        aes_decrypt_block(key1, block, block);

        /* P = P' ⊕ Tweak */
        for (u32 j = 0; j < 16; j++) blk[j] = block[j] ^ tweak[j];

        /* Tweak = Tweak * α (in GF(2^128)) */
        gf128_mulx(tweak);
    }
}

/* ==================== PBKDF2-HMAC-SHA256 ==================== */

/* F(...) = HMAC-SHA256(PRF, P || i) where PRF = password
  Simplified: we use the built-in HMAC-SHA256 with a salt prefix.
  Returns 0 on success, -1 if the salt does not fit the staging buffer.
  Callers must check: on failure `out` is left untouched, and treating that
  as success would derive keys from an all-zero buffer. */
static int pbkdf2_hmac_sha256(const u8* password, u32 pwd_len,
                               const u8* salt, u32 salt_len,
                               u32 iterations, u8* out, u32 out_len) {
    u32 block_count = (out_len + FDE_HASH_SIZE - 1) / FDE_HASH_SIZE;
    u32 remaining = out_len;

    /* salt_block holds salt || INT32_BE(block); reject anything that would
     * overrun it rather than truncating the salt (which would silently weaken
     * the derivation).  Every in-tree caller passes FDE_SALT_SIZE. */
    if (!salt && salt_len) return -1;
    if (salt_len > FDE_SECTOR_SIZE - 4u) return -1;

    for (u32 block = 1; block <= block_count && remaining > 0; block++) {
        /* U_1 = HMAC(password, salt || INT_BE(block)) */
        u8 u[FDE_HASH_SIZE];
        u8 t[FDE_HASH_SIZE];
        u8 salt_block[FDE_SECTOR_SIZE];
        u32 salt_block_len = salt_len + 4;
        u8* sp = salt_block;

        /* Build salt || INT32_BE(block) */
        memcpy(sp, salt, salt_len);
        sp += salt_len;
        sp[0] = (u8)(block >> 24);
        sp[1] = (u8)(block >> 16);
        sp[2] = (u8)(block >> 8);
        sp[3] = (u8)block;

        /* U_1 */
        hmac_sha256(password, pwd_len, salt_block, salt_block_len, u);
        memcpy(t, u, FDE_HASH_SIZE);

        /* U_2 .. U_iterations */
        for (u32 i = 1; i < iterations; i++) {
            hmac_sha256(password, pwd_len, u, FDE_HASH_SIZE, u);
            for (u32 j = 0; j < FDE_HASH_SIZE; j++) t[j] ^= u[j];
        }

        /* Copy to output */
        u32 copy = (remaining > FDE_HASH_SIZE) ? FDE_HASH_SIZE : remaining;
        memcpy(out + (block - 1) * FDE_HASH_SIZE, t, copy);
        remaining -= copy;
    }
    return 0;
}

/* ==================== FDE API Implementation ==================== */

void fde_init(void) {
    memset(&g_fde, 0, sizeof(g_fde));
    g_fde.start_sector = FDE_HEADER_SECTORS;
    serial_writeln("[FDE] subsystem initialized");
}

int fde_is_active(void) {
    return g_fde.active ? 1 : 0;
}

int fde_format(u32 dev_id, u32 start_lba, u32 total_lba, const char* password) {
    fde_header_t* hdr;
    u32 pwd_len;

    if (!password) return FDE_ERR_MEM;
    pwd_len = str_len(password);
    if (pwd_len == 0 || pwd_len > FDE_MAX_PASSWORD) return FDE_ERR_MEM;
    if (total_lba < FDE_HEADER_SECTORS * 2) return FDE_ERR_MEM;

    /* Check if already formatted */
    memset(g_fde_header_buf, 0, FDE_SECTOR_SIZE);
    if (disk_read_sector_physical(start_lba, g_fde_header_buf) != 0) return FDE_ERR_IO;
    
    /* Zero out the key buffer for fresh derivation */
    memset(g_fde_key_buf, 0, FDE_KEY_SIZE);
    hdr = (fde_header_t*)g_fde_header_buf;
    if (hdr->magic == FDE_MAGIC) return FDE_ERR_ALREADY;

    /* Fill in header (zero first, then populate fields) */
    memset(g_fde_header_buf, 0, FDE_SECTOR_SIZE);
    hdr = (fde_header_t*)g_fde_header_buf;
    hdr->magic = FDE_MAGIC;
    entropy_fill(hdr->salt, FDE_SALT_SIZE);
    hdr->pbkdf2_iterations = FDE_PBKDF2_ITERATIONS;

    /* Derive keys from password */
    if (pbkdf2_hmac_sha256((const u8*)password, pwd_len,
                           hdr->salt, FDE_SALT_SIZE,
                           FDE_PBKDF2_ITERATIONS,
                           g_fde_key_buf, FDE_KEY_SIZE) != 0) {
        return FDE_ERR_MEM;
    }

    /* Store keys in our state */
    memcpy(g_fde.key1, g_fde_key_buf, 32);
    memcpy(g_fde.key2, g_fde_key_buf + 32, 32);

    /* Create a verifier: encrypt a zeroed sector and hash it */
    memset(g_fde_work_buf, 0, FDE_SECTOR_SIZE);
    /* Use sector number 0xFFFFFFFF as the "verification sector" */
    aes_xts_encrypt_sector(g_fde.key1, g_fde.key2, 0xFFFFFFFFULL, g_fde_work_buf);
    sha256(g_fde_work_buf, FDE_SECTOR_SIZE, hdr->verifier);

    /* Write header to disk (physical — bypasses FDE hook).
       Sector 0 carries the magic/salt/verifier built above; the remaining
       header sectors are zero.  g_fde_header_buf is a single sector, so we
       write sector 0 from it and reuse it (zeroed) for sectors 1..N. */
    {
        u32 start = start_lba;
        if (disk_write_sector_physical(start, (u8*)hdr) != 0) return FDE_ERR_IO;
        memset(g_fde_header_buf, 0, FDE_SECTOR_SIZE);
        for (u32 i = 1; i < FDE_HEADER_SECTORS; i++) {
            if (disk_write_sector_physical(start + i, g_fde_header_buf) != 0) return FDE_ERR_IO;
        }
    }

    /* Initialize state */
    g_fde.dev_id = dev_id;
    g_fde.start_lba = start_lba;
    g_fde.total_sectors = total_lba - FDE_HEADER_SECTORS;
    g_fde.active = 1;
    g_fde.initialized = 1;
    
    /* Clear key buffer — keys are now in g_fde.key1/key2 */
    memset(g_fde_key_buf, 0, FDE_KEY_SIZE);

    serial_writeln("[FDE] format complete, encryption active");
    return FDE_OK;
}

int fde_unlock(u32 dev_id, u32 start_lba, const char* password) {
    fde_header_t* hdr;
    u8 computed_verifier[FDE_HASH_SIZE];
    u32 pwd_len;

    if (!password) return FDE_ERR_MEM;
    pwd_len = str_len(password);
    if (pwd_len == 0 || pwd_len > FDE_MAX_PASSWORD) return FDE_ERR_MEM;

    /* Read the header (physical — bypasses FDE hook to read the raw FDE header) */
    memset(g_fde_header_buf, 0, FDE_SECTOR_SIZE);
    if (disk_read_sector_physical(start_lba, g_fde_header_buf) != 0) return FDE_ERR_IO;
    hdr = (fde_header_t*)g_fde_header_buf;

    /* Verify magic */
    if (hdr->magic != FDE_MAGIC) return FDE_ERR_MAGIC;

    /* The iteration count comes from the on-disk header. Reject values outside
     * the supported band: 0 would collapse derivation to a single HMAC, and an
     * enormous value would hang the unlock path indefinitely. */
    if (hdr->pbkdf2_iterations < FDE_PBKDF2_MIN_ITER ||
        hdr->pbkdf2_iterations > FDE_PBKDF2_MAX_ITER) {
        serial_writeln("[FDE] header rejected: bad PBKDF2 iteration count");
        return FDE_ERR_MAGIC;
    }

    /* Zero out the key buffer for fresh derivation */
    memset(g_fde_key_buf, 0, FDE_KEY_SIZE);

    /* Derive keys from password */
    if (pbkdf2_hmac_sha256((const u8*)password, pwd_len,
                           hdr->salt, FDE_SALT_SIZE,
                           hdr->pbkdf2_iterations,
                           g_fde_key_buf, FDE_KEY_SIZE) != 0) {
        return FDE_ERR_MEM;
    }

    /* Store keys temporarily */
    memcpy(g_fde.key1, g_fde_key_buf, 32);
    memcpy(g_fde.key2, g_fde_key_buf + 32, 32);

    /* Verify by reproducing exactly what fde_format() hashed: the *encryption*
     * of a zeroed sector under the verification sector number.  Decrypting
     * here instead would derive an unrelated value, so no correct password
     * could ever match the stored verifier. */
    memset(g_fde_work_buf, 0, FDE_SECTOR_SIZE);
    aes_xts_encrypt_sector(g_fde.key1, g_fde.key2, 0xFFFFFFFFULL, g_fde_work_buf);
    sha256(g_fde_work_buf, FDE_SECTOR_SIZE, computed_verifier);

    if (!crypto_equal(computed_verifier, hdr->verifier, FDE_HASH_SIZE)) {
        /* Wrong password - clear keys */
        memset(&g_fde, 0, sizeof(g_fde));
        serial_writeln("[FDE] authentication FAILED");
        return FDE_ERR_AUTH;
    }

    /* Initialize state */
    g_fde.dev_id = dev_id;
    g_fde.start_lba = start_lba;
    /* Get total sector count from disk info */
    {
        disk_info_t di;
        if (disk_info(dev_id, &di) == 0) {
            g_fde.total_sectors = di.total_sectors_low - FDE_HEADER_SECTORS;
        } else {
            g_fde.total_sectors = 0;
        }
    }
    g_fde.active = 1;
    g_fde.initialized = 1;
    
    /* Clear key buffer — keys are now in g_fde.key1/key2 */
    memset(g_fde_key_buf, 0, FDE_KEY_SIZE);

    serial_writeln("[FDE] unlock SUCCESS, encryption active");
    return FDE_OK;
}

void fde_lock(void) {
    if (g_fde.active) {
        /* SECURITY: volatile write prevents the compiler from optimizing
         * away the key erasure (dead-store elimination). */
        volatile u8* k1 = (volatile u8*)g_fde.key1;
        volatile u8* k2 = (volatile u8*)g_fde.key2;
        for (u32 i = 0u; i < 32u; i++) { k1[i] = 0u; k2[i] = 0u; }
        g_fde.active = 0;
        g_fde.initialized = 0;
        serial_writeln("[FDE] locked (keys cleared)");
    }
}

int fde_read_sector(u32 lba, u8* out512) {
    if (!g_fde.active || !g_fde.initialized) return FDE_ERR_NOT_INIT;
    if (!out512) return FDE_ERR_MEM;
    /* Confine the request to the encrypted data region. Without this an
     * out-of-range lba lets phys_lba wrap or land outside the region, exposing
     * the FDE header or neighbouring partitions. */
    if (lba >= g_fde.total_sectors) return FDE_ERR_IO;

    /* Read physical sector (bypass FDE hook to avoid recursion) */
    u32 phys_lba = g_fde.start_lba + FDE_HEADER_SECTORS + lba;
    if (disk_read_sector_physical(phys_lba, out512) != 0) return FDE_ERR_IO;

    /* Decrypt */
    aes_xts_decrypt_sector(g_fde.key1, g_fde.key2, (u64)lba, out512);

    return FDE_OK;
}

int fde_write_sector(u32 lba, const u8* in512) {
    if (!g_fde.active || !g_fde.initialized) return FDE_ERR_NOT_INIT;
    if (!in512) return FDE_ERR_MEM;
    if (lba >= g_fde.total_sectors) return FDE_ERR_IO;   /* see fde_read_sector */

    /* Copy and encrypt */
    memcpy(g_fde_work_buf, in512, FDE_SECTOR_SIZE);
    aes_xts_encrypt_sector(g_fde.key1, g_fde.key2, (u64)lba, g_fde_work_buf);

    /* Write physical sector (bypass FDE hook to avoid recursion) */
    u32 phys_lba = g_fde.start_lba + FDE_HEADER_SECTORS + lba;
    if (disk_write_sector_physical(phys_lba, g_fde_work_buf) != 0) return FDE_ERR_IO;

    return FDE_OK;
}

int fde_status(u32* out_total_sectors) {
    if (out_total_sectors) *out_total_sectors = g_fde.total_sectors;
    if (!g_fde.active) return FDE_ERR_NOT_INIT;
    return FDE_OK;
}

void fde_encrypted_range(u32* out_start_lba, u32* out_sector_count) {
    if (out_start_lba) *out_start_lba = g_fde.start_lba + FDE_HEADER_SECTORS;
    if (out_sector_count) *out_sector_count = g_fde.total_sectors;
}
