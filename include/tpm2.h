#ifndef TPM2_H
#define TPM2_H

#include "types.h"

#define TPM2_MMIO_BASE      0xFED40000ULL
#define TPM2_PCR_DIGEST_LEN 32u /* SHA-256 */
#define TPM2_NUM_PCRS       24u

#define TPM2_OK              0
#define TPM2_ERR_ABSENT     -1
#define TPM2_ERR_TIMEOUT    -2
#define TPM2_ERR_FAIL       -3

typedef struct {
    u8 present;
    u8 locality;
    u32 pcr_mask;
    u8 pcr_digests[TPM2_NUM_PCRS][TPM2_PCR_DIGEST_LEN];
} tpm2_state_t;

void tpm2_init(void);
int tpm2_is_present(void);
int tpm2_pcr_read(u32 pcr_index, u8 out_digest[TPM2_PCR_DIGEST_LEN]);
int tpm2_seal_key(const u8 in_key[32], u32 pcr_mask, u8 out_sealed[64]);
int tpm2_unseal_key(const u8 in_sealed[64], u32 pcr_mask, u8 out_key[32]);

#endif /* TPM2_H */
