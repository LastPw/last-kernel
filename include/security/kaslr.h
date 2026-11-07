/*
 * =============================================================================
 * LastKernel — Advanced Kernel Security Subsystem
 * File: include/security/kaslr.h
 * Description: Kernel Address Space Layout Randomization (KASLR) Interface
 * Author: Taha Vaziry
 * =============================================================================
 */

#ifndef KASLR_H
#define KASLR_H

#include "types.h"

/* KASLR Entropy Sources */
#define KASLR_ENTROPY_NONE       0u
#define KASLR_ENTROPY_TSC_RTC    1u
#define KASLR_ENTROPY_RDRAND     2u
#define KASLR_ENTROPY_RDSEED     3u

/* Granularities and Alignment Boundaries (x86_64) */
#define KASLR_PAGE_SIZE          4096ULL
#define KASLR_HUGE_PAGE_2M       0x200000ULL       /* 2 MiB boundary */
#define KASLR_HUGE_PAGE_1G       0x40000000ULL     /* 1 GiB boundary */
#define KASLR_SLOT_SIZE_512G     0x8000000000ULL   /* 512 GiB PML4 slot size */

/* HHDM Randomization Boundaries */
#define KASLR_DEFAULT_HHDM       0xFFFF800000000000ULL
#define KASLR_HHDM_MAX_SLOTS     16u               /* PML4 slots 256..271 */

/* Kernel Heap Randomization Boundaries */
#define KASLR_HEAP_MAX_DELTA     (64ULL * 1024ULL * 1024ULL) /* 64 MiB variation */

/* Dynamic Driver/Module Virtual Space */
#define KASLR_MODULE_BASE_MIN    0xFFFFFFFF88000000ULL
#define KASLR_MODULE_BASE_MAX    0xFFFFFFFFB0000000ULL

/* Userland ASLR Constraints */
#define KASLR_USER_STACK_TOP_DEFAULT  0x00007FFFFFFFE000ULL
#define KASLR_USER_MMAP_BASE_DEFAULT  0x00007F0000000000ULL
#define KASLR_USER_BRK_BASE_DEFAULT   0x0000000001000000ULL

typedef struct {
    u8  enabled;
    u8  entropy_source;
    u16 user_aslr_active;
    u32 total_randomizations;
    u64 raw_entropy_seed;
    u64 hhdm_slot;
    u64 hhdm_offset_delta;
    u64 actual_hhdm_base;
    u64 kernel_heap_delta;
    u64 module_base;
    u64 user_stack_delta;
    u64 user_mmap_delta;
    u64 user_brk_delta;
} kaslr_status_t;

/* Core KASLR Lifecycle */
void kaslr_early_init(const char* cmdline);
int  kaslr_is_enabled(void);
void kaslr_get_status(kaslr_status_t* out);
const char* kaslr_entropy_source_name(u8 src);

/* Kernel Space Randomizers */
u64  kaslr_randomize_hhdm(u64 default_hhdm);
u64  kaslr_randomize_heap(u64 default_heap);
u64  kaslr_randomize_module(u64 default_module_base);

/* User Space ASLR Randomizers */
u64  kaslr_randomize_user_stack(u64 default_stack_top);
u64  kaslr_randomize_user_mmap(u64 default_mmap_base);
u64  kaslr_randomize_user_heap(u64 default_brk);

/* Helper for Aligned Random Value within Range */
u64  kaslr_gen_random_range(u64 min_val, u64 max_val, u64 alignment);

/* Diagnostic & Telemetry */
void kaslr_dump_info(void);

#endif /* KASLR_H */
