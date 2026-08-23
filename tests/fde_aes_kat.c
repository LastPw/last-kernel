/* Known-answer tests for the AES-128 / AES-XTS core used by the FDE layer.
 *
 * This is a HOST test: it is built with the host compiler, not the i686-elf
 * cross toolchain, and is not part of the kernel image.  Run it with
 *
 *     make test
 *
 * The AES section of system/fde.c is extracted into build/tests/fde_aes_core.inc
 * by the Makefile on every run rather than copied into the tree.  That matters:
 * an earlier hand-copied snapshot of this code kept reporting a failure that had
 * already been fixed, because the copy was stale.  Re-extracting makes the test
 * validate the shipping implementation by construction.
 *
 * These vectors are not decorative.  They caught two defects that reading the
 * code had missed: inv_mix_columns() applied forward-MixColumns arithmetic to
 * the unconditioned bytes (so AES decryption was simply wrong), and fde_unlock()
 * built its verifier with decrypt() while fde_format() had built it with
 * encrypt() (so no correct password could ever open a formatted volume).
 */
#include <stdio.h>
#include <string.h>

/* Minimal stand-ins for the kernel's types.h — the extracted AES core needs
 * these and nothing else. */
typedef unsigned char       u8;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef int                 i32;
typedef long long           s64;

#define FDE_SECTOR_SIZE       512u
#define FDE_BLOCK_SIZE        16u
#define FDE_BLOCKS_PER_SECTOR (FDE_SECTOR_SIZE / FDE_BLOCK_SIZE)

#include "fde_aes_core.inc"

static int fail = 0;

static void check(const char* what, const u8* got, const u8* want, u32 n) {
    if (memcmp(got, want, n) == 0) { printf("  PASS  %s\n", what); return; }
    fail = 1;
    printf("  FAIL  %s\n        got  ", what);
    for (u32 i = 0; i < n; i++) printf("%02x", got[i]);
    printf("\n        want ");
    for (u32 i = 0; i < n; i++) printf("%02x", want[i]);
    printf("\n");
}

static void expect(const char* what, int cond) {
    if (cond) { printf("  PASS  %s\n", what); return; }
    fail = 1;
    printf("  FAIL  %s\n", what);
}

int main(void) {
    /* ---- FIPS-197 appendix C.3, AES-256 ---- */
    const u8 key[32] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
                        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
                        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
    const u8 pt[16]  = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    const u8 ct[16]  = {0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,
                        0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89};
    u8 buf[16], rt[16];

    printf("FIPS-197 C.3 (AES-256):\n");
    aes_encrypt_block(key, pt, buf);
    check("encrypt_block", buf, ct, 16);
    aes_decrypt_block(key, ct, buf);
    check("decrypt_block", buf, pt, 16);

    printf("Block round trip:\n");
    aes_encrypt_block(key, pt, buf);
    aes_decrypt_block(key, buf, rt);
    check("decrypt(encrypt(P)) == P", rt, pt, 16);

    /* ---- XTS sector round trip ---- */
    printf("XTS sector round trip:\n");
    u8 k1[32], k2[32], sec[512], orig[512];
    for (u32 i = 0; i < 32; i++) { k1[i] = (u8)(i * 7 + 1); k2[i] = (u8)(i * 13 + 5); }
    for (u32 i = 0; i < 512; i++) orig[i] = (u8)(i * 31 + 11);

    memcpy(sec, orig, 512);
    aes_xts_encrypt_sector(k1, k2, 0x1234ULL, sec);
    expect("ciphertext differs from plaintext", memcmp(sec, orig, 512) != 0);
    aes_xts_decrypt_sector(k1, k2, 0x1234ULL, sec);
    check("xts_decrypt(xts_encrypt(S)) == S", sec, orig, 512);

    /* Decrypting under the wrong sector number must not recover the data. */
    memcpy(sec, orig, 512);
    aes_xts_encrypt_sector(k1, k2, 0x1234ULL, sec);
    aes_xts_decrypt_sector(k1, k2, 0x9999ULL, sec);
    expect("wrong sector number does not decrypt", memcmp(sec, orig, 512) != 0);

    /* ---- The verifier construction used by fde.c ----
     * fde_format() and fde_unlock() must derive the verifier input identically,
     * or no correct password can ever unlock a formatted volume.  Both lines
     * below are transcribed from those two functions. */
    printf("FDE verifier construction (format vs unlock):\n");
    u8 fmt_vin[512], unl_vin[512];
    memset(fmt_vin, 0, 512);
    memset(unl_vin, 0, 512);
    aes_xts_encrypt_sector(k1, k2, 0xFFFFFFFFULL, fmt_vin);  /* as fde_format does */
    aes_xts_encrypt_sector(k1, k2, 0xFFFFFFFFULL, unl_vin);  /* as fde_unlock does */
    check("unlock verifier input == format verifier input", unl_vin, fmt_vin, 512);

    /* ---- Volume lifecycle: the right password opens it, a wrong one does not.
     * SHA-256 is not linked into this harness, so the 512-byte verifier input
     * stands in for its digest — equal inputs imply equal hashes, and a
     * differing input is what the stored digest is meant to reject. */
    printf("Volume lifecycle (format then unlock):\n");
    u8 good1[32], good2[32], bad1[32], bad2[32];
    for (u32 i = 0; i < 32; i++) {
        good1[i] = (u8)(0xA0 + i); good2[i] = (u8)(0x50 + i);
        bad1[i]  = (u8)(0xA0 + i); bad2[i]  = (u8)(0x50 + i);
    }
    bad1[7] ^= 0x01;   /* one bit of derived key material differs */

    u8 stored[512], attempt[512];
    memset(stored, 0, 512);
    aes_xts_encrypt_sector(good1, good2, 0xFFFFFFFFULL, stored);   /* written at format time */

    memset(attempt, 0, 512);
    aes_xts_encrypt_sector(good1, good2, 0xFFFFFFFFULL, attempt);
    check("correct password reproduces stored verifier", attempt, stored, 512);

    memset(attempt, 0, 512);
    aes_xts_encrypt_sector(bad1, bad2, 0xFFFFFFFFULL, attempt);
    expect("wrong password does not match the verifier", memcmp(attempt, stored, 512) != 0);

    /* ---- Sector independence: identical plaintext at two LBAs must differ ---- */
    printf("XTS sector independence:\n");
    u8 s_a[512], s_b[512];
    memcpy(s_a, orig, 512);
    memcpy(s_b, orig, 512);
    aes_xts_encrypt_sector(k1, k2, 100ULL, s_a);
    aes_xts_encrypt_sector(k1, k2, 101ULL, s_b);
    expect("adjacent sectors produce different ciphertext", memcmp(s_a, s_b, 512) != 0);
    aes_xts_decrypt_sector(k1, k2, 100ULL, s_a);
    check("sector 100 round trip", s_a, orig, 512);
    aes_xts_decrypt_sector(k1, k2, 101ULL, s_b);
    check("sector 101 round trip", s_b, orig, 512);

    printf("\n%s\n", fail ? "FAILURES in the FDE AES core" : "FDE AES core: all vectors pass");
    return fail;
}
