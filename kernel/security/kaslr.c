/*
 * =============================================================================
 * LastKernel — Advanced Kernel Security Subsystem
 * File: kernel/security/kaslr.c
 * Description: Kernel Address Space Layout Randomization (KASLR) Engine
 * Author: Taha Vaziry
 * =============================================================================
 */

#include "security/kaslr.h"

#ifdef HOST_TEST
#include <string.h>
#include <stdio.h>
#else
#include "string.h"
#include "serial.h"
#endif

/* ── Global State ── */
static kaslr_status_t s_status;
static u64 s_prng_state = 0x85435B9114705F1CULL;
static u8  s_initialized = 0;

/* ── SplitMix64 PRNG Helper ── */
static u64 kaslr_prng_next(void) {
    s_prng_state += 0x9E3779B97F4A7C15ULL;
    u64 z = s_prng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ── Hardware Entropy Extraction ── */
static u64 kaslr_read_tsc(void) {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(_MSC_VER)
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | (u64)lo;
#else
    return 0x5EED12349876ULL;
#endif
}

static int kaslr_probe_hw_entropy(u64* out_val, u8* out_source) {
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(_MSC_VER)
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;

    /* Check extended features for RDSEED (CPUID leaf 7, EBX bit 18) */
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    if (ebx & (1u << 18)) {
        u64 rdseed_val = 0;
        unsigned char ok = 0;
        for (int retry = 0; retry < 32; retry++) {
            __asm__ __volatile__("rdseed %0; setc %1" : "=r"(rdseed_val), "=qm"(ok));
            if (ok) {
                *out_val = rdseed_val;
                *out_source = KASLR_ENTROPY_RDSEED;
                return 0;
            }
        }
    }

    /* Check basic features for RDRAND (CPUID leaf 1, ECX bit 30) */
    __asm__ __volatile__("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    if (ecx & (1u << 30)) {
        u64 rdrand_val = 0;
        unsigned char ok = 0;
        for (int retry = 0; retry < 16; retry++) {
            __asm__ __volatile__("rdrand %0; setc %1" : "=r"(rdrand_val), "=qm"(ok));
            if (ok) {
                *out_val = rdrand_val;
                *out_source = KASLR_ENTROPY_RDRAND;
                return 0;
            }
        }
    }
#endif

    /* Fallback: TSC combined with timer state */
    u64 tsc = kaslr_read_tsc();
    *out_val = tsc ^ (tsc >> 17) ^ 0xA5A5A5A55A5A5A5AULL;
    *out_source = KASLR_ENTROPY_TSC_RTC;
    return 0;
}

/* ── Name of Entropy Source ── */
const char* kaslr_entropy_source_name(u8 src) {
    switch (src) {
        case KASLR_ENTROPY_RDSEED:  return "Intel/AMD RDSEED (True HW Entropy)";
        case KASLR_ENTROPY_RDRAND:  return "Intel/AMD RDRAND (HW Cryptographic RNG)";
        case KASLR_ENTROPY_TSC_RTC: return "High-Resolution TSC + Jitter Fallback";
        default:                    return "None (Deterministic Static Layout)";
    }
}

/* ── Subsystem Initialization ── */
void kaslr_early_init(const char* cmdline) {
    memset(&s_status, 0, sizeof(s_status));

    /* Default: KASLR is enabled for enterprise security posture */
    s_status.enabled = 1;
    s_status.user_aslr_active = 1;

    /* Parse Kernel Boot Parameters */
    if (cmdline && *cmdline) {
        if (strstr(cmdline, "nokaslr") != NULL) {
            s_status.enabled = 0;
            s_status.user_aslr_active = 0;
        } else if (strstr(cmdline, "kaslr") != NULL) {
            s_status.enabled = 1;
            s_status.user_aslr_active = 1;
        }
    }

    if (!s_status.enabled) {
        s_status.entropy_source = KASLR_ENTROPY_NONE;
        s_status.actual_hhdm_base = KASLR_DEFAULT_HHDM;
        s_status.module_base = KASLR_MODULE_BASE_MIN;
        s_initialized = 1;
#ifndef HOST_TEST
        serial_writeln("[KASLR] Disabled via 'nokaslr' kernel boot parameter.");
#endif
        return;
    }

    /* Harvest Hardware Entropy */
    u64 hw_val = 0;
    u8  hw_src = KASLR_ENTROPY_NONE;
    kaslr_probe_hw_entropy(&hw_val, &hw_src);

    /* Mix with high-resolution TSC to prevent replay */
    u64 tsc = kaslr_read_tsc();
    s_status.raw_entropy_seed = hw_val ^ ((tsc << 13) | (tsc >> 51));
    s_status.entropy_source = hw_src;

    /* Seed the Internal PRNG */
    s_prng_state = s_status.raw_entropy_seed;
    if (s_prng_state == 0) s_prng_state = 0xDEADBEEFCAFEBABEULL;

    /* Pre-compute randomized bases */
    s_status.actual_hhdm_base = kaslr_randomize_hhdm(KASLR_DEFAULT_HHDM);
    s_status.kernel_heap_delta = kaslr_randomize_heap(0);
    s_status.module_base = kaslr_randomize_module(KASLR_MODULE_BASE_MIN);
    s_status.user_stack_delta = kaslr_randomize_user_stack(KASLR_USER_STACK_TOP_DEFAULT) ^ KASLR_USER_STACK_TOP_DEFAULT;
    s_status.user_mmap_delta = kaslr_randomize_user_mmap(KASLR_USER_MMAP_BASE_DEFAULT) ^ KASLR_USER_MMAP_BASE_DEFAULT;
    s_status.user_brk_delta = kaslr_randomize_user_heap(KASLR_USER_BRK_BASE_DEFAULT) ^ KASLR_USER_BRK_BASE_DEFAULT;

    s_initialized = 1;

#ifndef HOST_TEST
    serial_writeln("[KASLR] Kernel Address Space Layout Randomization Initialized");
    serial_write("[KASLR] Active Entropy Source: ");
    serial_writeln(kaslr_entropy_source_name(s_status.entropy_source));
    serial_write("[KASLR] HHDM Base: 0x");
    serial_write_hex((u32)(s_status.actual_hhdm_base >> 32));
    serial_write_hex((u32)s_status.actual_hhdm_base);
    serial_write(" (Slot: ");
    serial_write_dec((u32)s_status.hhdm_slot);
    serial_writeln(")");
#endif
}

int kaslr_is_enabled(void) {
    if (!s_initialized) kaslr_early_init(NULL);
    return s_status.enabled;
}

void kaslr_get_status(kaslr_status_t* out) {
    if (!out) return;
    if (!s_initialized) kaslr_early_init(NULL);
    *out = s_status;
}

/* ── Kernel Space Randomization ── */

u64 kaslr_randomize_hhdm(u64 default_hhdm) {
    if (!s_status.enabled) {
        s_status.actual_hhdm_base = default_hhdm;
        s_status.hhdm_slot = 0;
        s_status.hhdm_offset_delta = 0;
        return default_hhdm;
    }

    /* Pick a random PML4 slot among the reserved 16 slots (0..15) */
    u64 rnd = kaslr_prng_next();
    u64 slot = rnd % KASLR_HHDM_MAX_SLOTS;

    s_status.hhdm_slot = slot;
    s_status.hhdm_offset_delta = slot * KASLR_SLOT_SIZE_512G;
    s_status.actual_hhdm_base = default_hhdm + s_status.hhdm_offset_delta;
    s_status.total_randomizations++;

    return s_status.actual_hhdm_base;
}

u64 kaslr_randomize_heap(u64 default_heap) {
    if (!s_status.enabled) return default_heap;

    /* Up to 64 MiB delta, aligned to 2 MiB Huge Page boundary */
    u64 rnd = kaslr_prng_next();
    u64 delta = (rnd % (KASLR_HEAP_MAX_DELTA / KASLR_HUGE_PAGE_2M)) * KASLR_HUGE_PAGE_2M;

    s_status.kernel_heap_delta = delta;
    s_status.total_randomizations++;

    return default_heap + delta;
}

u64 kaslr_randomize_module(u64 default_module_base) {
    if (!s_status.enabled) return default_module_base;

    u64 range = KASLR_MODULE_BASE_MAX - KASLR_MODULE_BASE_MIN;
    u64 rnd = kaslr_prng_next();
    u64 offset = (rnd % (range / KASLR_HUGE_PAGE_2M)) * KASLR_HUGE_PAGE_2M;

    s_status.module_base = KASLR_MODULE_BASE_MIN + offset;
    s_status.total_randomizations++;

    return s_status.module_base;
}

/* ── User Space ASLR Randomization ── */

u64 kaslr_randomize_user_stack(u64 default_stack_top) {
    if (!s_status.enabled || !s_status.user_aslr_active) return default_stack_top;

    /* Randomize stack top downwards by 0..16 MiB in 4 KiB page increments */
    u64 rnd = kaslr_prng_next();
    u64 pages = rnd % 4096u; /* up to 16 MiB */
    u64 delta = pages * KASLR_PAGE_SIZE;

    s_status.total_randomizations++;
    return default_stack_top - delta;
}

u64 kaslr_randomize_user_mmap(u64 default_mmap_base) {
    if (!s_status.enabled || !s_status.user_aslr_active) return default_mmap_base;

    /* Randomize mmap base by 0..64 MiB in 4 KiB page increments */
    u64 rnd = kaslr_prng_next();
    u64 pages = rnd % 16384u; /* up to 64 MiB */
    u64 delta = pages * KASLR_PAGE_SIZE;

    s_status.total_randomizations++;
    return default_mmap_base + delta;
}

u64 kaslr_randomize_user_heap(u64 default_brk) {
    if (!s_status.enabled || !s_status.user_aslr_active) return default_brk;

    /* Randomize initial brk gap by 0..8 MiB in 4 KiB increments */
    u64 rnd = kaslr_prng_next();
    u64 pages = rnd % 2048u; /* up to 8 MiB */
    u64 delta = pages * KASLR_PAGE_SIZE;

    s_status.total_randomizations++;
    return default_brk + delta;
}

u64 kaslr_gen_random_range(u64 min_val, u64 max_val, u64 alignment) {
    if (alignment == 0) alignment = 1;
    if (min_val >= max_val) return min_val;

    u64 range = (max_val - min_val) / alignment;
    if (range == 0) return min_val;

    u64 rnd = kaslr_prng_next();
    u64 val = min_val + ((rnd % range) * alignment);
    s_status.total_randomizations++;
    return val;
}

/* ── Telemetry & Diagnostic Dump ── */
void kaslr_dump_info(void) {
#ifdef HOST_TEST
    printf("=================================================================\n");
    printf("   LastKernel Address Space Layout Randomization (KASLR) Telemetry\n");
    printf("=================================================================\n");
    printf(" Status:                  %s\n", s_status.enabled ? "ACTIVE (ENFORCED)" : "DISABLED (NOKASLR)");
    printf(" Entropy Source:          %s\n", kaslr_entropy_source_name(s_status.entropy_source));
    printf(" Raw Entropy Seed:        0x%016llX\n", (unsigned long long)s_status.raw_entropy_seed);
    printf(" HHDM Canonical Base:     0x%016llX\n", (unsigned long long)KASLR_DEFAULT_HHDM);
    printf(" HHDM Active Base:        0x%016llX (PML4 Slot: %llu)\n", 
           (unsigned long long)s_status.actual_hhdm_base, (unsigned long long)s_status.hhdm_slot);
    printf(" HHDM Offset Delta:       +0x%016llX (+%llu GiB)\n", 
           (unsigned long long)s_status.hhdm_offset_delta, (unsigned long long)(s_status.hhdm_offset_delta >> 30));
    printf(" Kernel Heap Delta:       +0x%016llX (+%llu KiB)\n", 
           (unsigned long long)s_status.kernel_heap_delta, (unsigned long long)(s_status.kernel_heap_delta >> 10));
    printf(" Dynamic Module Base:     0x%016llX\n", (unsigned long long)s_status.module_base);
    printf(" Userland ASLR Status:    %s\n", s_status.user_aslr_active ? "ENABLED" : "DISABLED");
    printf(" Total Addresses Shielded: %u\n", s_status.total_randomizations);
    printf("=================================================================\n");
#else
    serial_writeln("=================================================================");
    serial_writeln("   LastKernel Address Space Layout Randomization (KASLR) Status ");
    serial_writeln("=================================================================");
    serial_write(" Status:                  ");
    serial_writeln(s_status.enabled ? "ACTIVE (ENFORCED)" : "DISABLED (NOKASLR)");
    serial_write(" Entropy Source:          ");
    serial_writeln(kaslr_entropy_source_name(s_status.entropy_source));
    serial_write(" HHDM Active Base:        0x");
    serial_write_hex((u32)(s_status.actual_hhdm_base >> 32));
    serial_write_hex((u32)s_status.actual_hhdm_base);
    serial_write(" (PML4 Slot: ");
    serial_write_dec((u32)s_status.hhdm_slot);
    serial_writeln(")");
    serial_write(" Kernel Heap Delta:       +0x");
    serial_write_hex((u32)(s_status.kernel_heap_delta >> 32));
    serial_write_hex((u32)s_status.kernel_heap_delta);
    serial_writeln("");
    serial_write(" Dynamic Module Base:     0x");
    serial_write_hex((u32)(s_status.module_base >> 32));
    serial_write_hex((u32)s_status.module_base);
    serial_writeln("");
    serial_write(" Userland ASLR Status:    ");
    serial_writeln(s_status.user_aslr_active ? "ENABLED" : "DISABLED");
    serial_write(" Total Addresses Shielded: ");
    serial_write_dec(s_status.total_randomizations);
    serial_writeln("");
    serial_writeln("=================================================================");
#endif
}
