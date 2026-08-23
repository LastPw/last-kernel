#include "tpm2.h"
#include "sha256.h"
#include "string.h"
#include "serial.h"
#include "entropy.h"
#include "bootinfo.h"

#define TIS_REG_ACCESS          0x00u
#define TIS_REG_STS             0x18u
#define TIS_REG_DATA_FIFO       0x24u
#define TIS_REG_DID_VID         0xF00u

#define TIS_ACCESS_VALID        0x80u
#define TIS_ACCESS_ACTIVE_LOCAL 0x20u
#define TIS_ACCESS_REQUEST_USE  0x02u

#define TIS_STS_VALID           0x80u
#define TIS_STS_COMMAND_READY   0x40u
#define TIS_STS_DATA_AVAIL      0x10u

static tpm2_state_t g_tpm;

static inline u8 mmio_read8(uintptr_t addr) {
    return *(volatile u8*)addr;
}

static inline void mmio_write8(uintptr_t addr, u8 val) {
    *(volatile u8*)addr = val;
}

static inline u32 mmio_read32(uintptr_t addr) {
    return *(volatile u32*)addr;
}

void tpm2_init(void) {
    memset(&g_tpm, 0, sizeof(g_tpm));
    g_tpm.present = 1u;
    g_tpm.locality = 0u;
    g_tpm.pcr_mask = 0x000000FFu; // PCR 0..7

    for (u32 i = 0u; i < TPM2_NUM_PCRS; i++) {
        u8 pcr_seed[16] = { (u8)i, 0xAA, 0x55, 0x90 };
        sha256(pcr_seed, sizeof(pcr_seed), g_tpm.pcr_digests[i]);
    }
    serial_writeln("[TPM2] Software PCR Integrity Engine initialized (PCR 0-7 active)");
}

int tpm2_is_present(void) {
    return g_tpm.present ? 1 : 0;
}

int tpm2_pcr_read(u32 pcr_index, u8 out_digest[TPM2_PCR_DIGEST_LEN]) {
    if (!g_tpm.present || pcr_index >= TPM2_NUM_PCRS || !out_digest) return TPM2_ERR_ABSENT;
    memcpy(out_digest, g_tpm.pcr_digests[pcr_index], TPM2_PCR_DIGEST_LEN);
    return TPM2_OK;
}

int tpm2_seal_key(const u8 in_key[32], u32 pcr_mask, u8 out_sealed[64]) {
    if (!in_key || !out_sealed) return TPM2_ERR_FAIL;

    // Mix in_key with active PCR digests selected by pcr_mask
    sha256_ctx_t ctx;
    u8 pcr_binding[32];

    sha256_init(&ctx);
    for (u32 i = 0u; i < 8u; i++) {
        if ((pcr_mask & (1u << i)) != 0u) {
            sha256_update(&ctx, g_tpm.pcr_digests[i], TPM2_PCR_DIGEST_LEN);
        }
    }
    sha256_final(&ctx, pcr_binding);

    // XOR key material with pcr_binding to produce sealed blob
    for (u32 i = 0u; i < 32u; i++) {
        out_sealed[i] = in_key[i] ^ pcr_binding[i];
        out_sealed[i + 32u] = pcr_binding[i];
    }
    return TPM2_OK;
}

int tpm2_unseal_key(const u8 in_sealed[64], u32 pcr_mask, u8 out_key[32]) {
    if (!in_sealed || !out_key) return TPM2_ERR_FAIL;

    sha256_ctx_t ctx;
    u8 current_pcr_binding[32];

    sha256_init(&ctx);
    for (u32 i = 0u; i < 8u; i++) {
        if ((pcr_mask & (1u << i)) != 0u) {
            sha256_update(&ctx, g_tpm.pcr_digests[i], TPM2_PCR_DIGEST_LEN);
        }
    }
    sha256_final(&ctx, current_pcr_binding);

    // Verify PCR binding match
    if (!crypto_equal(current_pcr_binding, in_sealed + 32u, 32u)) {
        serial_writeln("[TPM2] unseal FAILED: PCR state mismatch (tampering detected)");
        return TPM2_ERR_FAIL;
    }

    for (u32 i = 0u; i < 32u; i++) {
        out_key[i] = in_sealed[i] ^ current_pcr_binding[i];
    }
    return TPM2_OK;
}
