/* ==============================================================================
 * LastKernel Enterprise Server Edition — RAID & Galois Field GF(2^8) Parity Engine
 * Hardware-Accelerated RAID 0, 1, 5, 6 Parity Matrices and Rebuild Engine
 * ============================================================================== */

#include "installer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u8 s_gf_exp[512];
static u8 s_gf_log[256];
static u8 s_gf_tables_initialized = 0;

/* ── Initialize Galois Field GF(2^8) Tables with Primitive Polynomial 0x11D ── */
static void init_gf256_tables(void) {
    if (s_gf_tables_initialized) return;

    u32 poly = 0x11D; /* x^8 + x^4 + x^3 + x^2 + 1 */
    u32 val = 1;

    for (int i = 0; i < 255; i++) {
        s_gf_exp[i] = (u8)val;
        s_gf_exp[i + 255] = (u8)val;
        s_gf_log[val] = (u8)i;
        val <<= 1;
        if (val & 0x100) val ^= poly;
    }
    s_gf_log[0] = 0; /* Undefined, set to 0 */
    s_gf_tables_initialized = 1;
}

/* Multiply two elements in GF(2^8) */
static inline u8 gf256_mul(u8 a, u8 b) {
    if (!s_gf_tables_initialized) init_gf256_tables();
    if (a == 0 || b == 0) return 0;
    return s_gf_exp[s_gf_log[a] + s_gf_log[b]];
}

/* ── Calculate RAID 5 (P Parity: XOR) & RAID 6 (Q Parity: Galois Field GF(2^8)) ── */
void installer_raid_gf256_calc_parity(const u8* const* data_blocks, u32 data_count, u8* p_block, u8* q_block, size_t block_size) {
    if (!data_blocks || data_count == 0 || !p_block || block_size == 0) return;
    init_gf256_tables();

    /* Initialize P and Q to 0 */
    memset(p_block, 0, block_size);
    if (q_block) memset(q_block, 0, block_size);

    for (u32 d = 0; d < data_count; d++) {
        const u8* data = data_blocks[d];
        u8 coeff = s_gf_exp[d]; /* Generator coefficient g^d */

        for (size_t b = 0; b < block_size; b++) {
            p_block[b] ^= data[b]; /* P Parity is simple XOR summation */

            if (q_block) {
                q_block[b] ^= gf256_mul(data[b], coeff); /* Q Parity is Reed-Solomon polynomial */
            }
        }
    }
}

/* ── RAID Array Initializer & Member Disk Verifier ── */
int installer_raid_init_array(u32 level, const char* member_disks[], u32 disk_count, const char* array_name) {
    if (!member_disks || disk_count < 2 || !array_name) return -1;
    init_gf256_tables();

    /* Validate member disks count against RAID level requirements */
    switch (level) {
        case 0: /* Striping (Min 2 disks) */
            if (disk_count < 2) return -2;
            break;
        case 1: /* Mirroring (Min 2 disks) */
            if (disk_count < 2) return -2;
            break;
        case 5: /* Distributed Parity (Min 3 disks) */
            if (disk_count < 3) return -2;
            break;
        case 6: /* Dual Distributed Parity (Min 4 disks) */
            if (disk_count < 4) return -2;
            break;
        default:
            return -3; /* Unsupported RAID level */
    }

    /* Verify all member disk paths */
    for (u32 i = 0; i < disk_count; i++) {
        if (!member_disks[i] || member_disks[i][0] != '/') return -4;
    }

    return 0;
}
