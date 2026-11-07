/*
 * =============================================================================
 * LastKernel — Advanced Kernel Security Subsystem
 * File: tests/kaslr_tests.c
 * Description: Automated Subsystem Test Matrix for Kernel Address Space Layout
 *              Randomization (KASLR)
 * Author: Taha Vaziry
 * =============================================================================
 */

#define HOST_TEST 1

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "security/kaslr.h"

/* ── Test 1: Verify Default Enablement and Explicit 'kaslr' Cmdline ── */
static void test_kaslr_default_and_explicit_enable(void) {
    printf("  [1/7] Testing KASLR default initialization & explicit cmdline enable... ");

    /* Pass empty cmdline: should default to enabled */
    kaslr_early_init("");
    assert(kaslr_is_enabled() == 1);

    kaslr_status_t st;
    kaslr_get_status(&st);
    assert(st.enabled == 1);
    assert(st.user_aslr_active == 1);
    assert(st.actual_hhdm_base >= KASLR_DEFAULT_HHDM);
    assert(st.actual_hhdm_base < (KASLR_DEFAULT_HHDM + (16ULL * KASLR_SLOT_SIZE_512G)));

    /* Explicit 'kaslr' in cmdline */
    kaslr_early_init("BOOT_MODE=fast kaslr quiet console=tty0");
    assert(kaslr_is_enabled() == 1);
    kaslr_get_status(&st);
    assert(st.enabled == 1);

    printf("PASS (Enforced by default & explicit)\n");
}

/* ── Test 2: Verify 'nokaslr' Disables Randomization ── */
static void test_kaslr_nokaslr_flag(void) {
    printf("  [2/7] Testing 'nokaslr' boot argument parameter parsing... ");

    kaslr_early_init("BOOT_MODE=safe nokaslr debug");
    assert(kaslr_is_enabled() == 0);

    kaslr_status_t st;
    kaslr_get_status(&st);
    assert(st.enabled == 0);
    assert(st.user_aslr_active == 0);
    assert(st.actual_hhdm_base == KASLR_DEFAULT_HHDM);
    assert(st.hhdm_offset_delta == 0);
    assert(st.hhdm_slot == 0);

    /* Test that randomizers return default values when disabled */
    u64 hhdm = kaslr_randomize_hhdm(KASLR_DEFAULT_HHDM);
    assert(hhdm == KASLR_DEFAULT_HHDM);

    u64 heap = kaslr_randomize_heap(0x10000000ULL);
    assert(heap == 0x10000000ULL);

    u64 mod = kaslr_randomize_module(KASLR_MODULE_BASE_MIN);
    assert(mod == KASLR_MODULE_BASE_MIN);

    u64 st_top = kaslr_randomize_user_stack(KASLR_USER_STACK_TOP_DEFAULT);
    assert(st_top == KASLR_USER_STACK_TOP_DEFAULT);

    u64 mmap = kaslr_randomize_user_mmap(KASLR_USER_MMAP_BASE_DEFAULT);
    assert(mmap == KASLR_USER_MMAP_BASE_DEFAULT);

    printf("PASS (Deterministic fixed addresses restored)\n");
}

/* ── Test 3: Verify HHDM Slot Selection and 512 GiB PML4 Alignment ── */
static void test_kaslr_hhdm_slots_and_alignment(void) {
    printf("  [3/7] Testing HHDM slot boundaries and 512 GiB PML4 alignment... ");

    kaslr_early_init("kaslr");
    assert(kaslr_is_enabled() == 1);

    kaslr_status_t st;
    kaslr_get_status(&st);

    /* Verify slot range is within 0..15 */
    assert(st.hhdm_slot < KASLR_HHDM_MAX_SLOTS);
    assert(st.hhdm_offset_delta == st.hhdm_slot * KASLR_SLOT_SIZE_512G);
    assert(st.actual_hhdm_base == KASLR_DEFAULT_HHDM + st.hhdm_offset_delta);

    /* Verify 512 GiB alignment */
    assert((st.actual_hhdm_base & (KASLR_SLOT_SIZE_512G - 1ULL)) == 0);

    /* Verify multiple calls keep alignment intact */
    for (int i = 0; i < 20; i++) {
        u64 val = kaslr_randomize_hhdm(KASLR_DEFAULT_HHDM);
        assert((val & (KASLR_SLOT_SIZE_512G - 1ULL)) == 0);
        assert(val >= KASLR_DEFAULT_HHDM);
        assert(val < (KASLR_DEFAULT_HHDM + (KASLR_HHDM_MAX_SLOTS * KASLR_SLOT_SIZE_512G)));
    }

    printf("PASS (Strict 512 GiB PML4 boundary satisfied)\n");
}

/* ── Test 4: Verify Kernel Heap Alignment and Bounds ── */
static void test_kaslr_heap_alignment_and_bounds(void) {
    printf("  [4/7] Testing Kernel Heap 2 MiB Huge Page alignment & bounds... ");

    kaslr_early_init("kaslr");

    for (int i = 0; i < 50; i++) {
        u64 base = 0xFFFF800010000000ULL;
        u64 randomized = kaslr_randomize_heap(base);

        /* Must be greater than or equal to base */
        assert(randomized >= base);
        /* Must not exceed base + 64 MiB */
        assert(randomized <= (base + KASLR_HEAP_MAX_DELTA));
        /* Delta must be aligned to 2 MiB */
        u64 delta = randomized - base;
        assert((delta % KASLR_HUGE_PAGE_2M) == 0);
    }

    printf("PASS (2 MiB boundary & 64 MiB limit enforced)\n");
}

/* ── Test 5: Verify Dynamic Module Virtual Space Bounds ── */
static void test_kaslr_module_alignment_and_bounds(void) {
    printf("  [5/7] Testing Dynamic Module Virtual Range [0x%016llX..0x%016llX]... ",
           (unsigned long long)KASLR_MODULE_BASE_MIN, (unsigned long long)KASLR_MODULE_BASE_MAX);

    kaslr_early_init("kaslr");

    for (int i = 0; i < 50; i++) {
        u64 mod_addr = kaslr_randomize_module(KASLR_MODULE_BASE_MIN);
        assert(mod_addr >= KASLR_MODULE_BASE_MIN);
        assert(mod_addr <= KASLR_MODULE_BASE_MAX);
        assert((mod_addr % KASLR_HUGE_PAGE_2M) == 0);
    }

    printf("PASS (Strictly contained and 2 MiB aligned)\n");
}

/* ── Test 6: Verify Userland Stack, Mmap, and Heap ASLR ── */
static void test_kaslr_userland_aslr_alignment(void) {
    printf("  [6/7] Testing Userland ASLR (Stack, Mmap, Brk page alignment)... ");

    kaslr_early_init("kaslr");

    for (int i = 0; i < 50; i++) {
        /* User stack */
        u64 stack = kaslr_randomize_user_stack(KASLR_USER_STACK_TOP_DEFAULT);
        assert(stack <= KASLR_USER_STACK_TOP_DEFAULT);
        assert((stack % KASLR_PAGE_SIZE) == 0);

        /* User mmap */
        u64 mmap = kaslr_randomize_user_mmap(KASLR_USER_MMAP_BASE_DEFAULT);
        assert(mmap >= KASLR_USER_MMAP_BASE_DEFAULT);
        assert((mmap % KASLR_PAGE_SIZE) == 0);

        /* User brk */
        u64 brk = kaslr_randomize_user_heap(KASLR_USER_BRK_BASE_DEFAULT);
        assert(brk >= KASLR_USER_BRK_BASE_DEFAULT);
        assert((brk % KASLR_PAGE_SIZE) == 0);
    }

    printf("PASS (4 KiB page-granularity verified)\n");
}

/* ── Test 7: Verify Aligned Random Range and Telemetry Accounting ── */
static void test_kaslr_telemetry_and_random_range(void) {
    printf("  [7/7] Testing Aligned Random Range generator & Telemetry Accounting... ");

    kaslr_early_init("kaslr");

    kaslr_status_t st1;
    kaslr_get_status(&st1);
    uint32_t count_before = st1.total_randomizations;

    /* Generate range between 0x100000 and 0x500000 with 64 KiB alignment */
    for (int i = 0; i < 30; i++) {
        u64 val = kaslr_gen_random_range(0x100000ULL, 0x500000ULL, 0x10000ULL);
        assert(val >= 0x100000ULL);
        assert(val <= 0x500000ULL);
        assert((val % 0x10000ULL) == 0);
    }

    kaslr_status_t st2;
    kaslr_get_status(&st2);
    assert(st2.total_randomizations == count_before + 30);

    /* Verify entropy source string */
    const char* src_name = kaslr_entropy_source_name(st2.entropy_source);
    assert(src_name != NULL && strlen(src_name) > 0);

    printf("PASS (Counters incremented & verified)\n");
}

int main(void) {
    printf("\n=================================================================\n");
    printf("   LastKernel Address Space Layout Randomization (KASLR) Suite   \n");
    printf("=================================================================\n");

    test_kaslr_default_and_explicit_enable();
    test_kaslr_nokaslr_flag();
    test_kaslr_hhdm_slots_and_alignment();
    test_kaslr_heap_alignment_and_bounds();
    test_kaslr_module_alignment_and_bounds();
    test_kaslr_userland_aslr_alignment();
    test_kaslr_telemetry_and_random_range();

    printf("\n");
    kaslr_dump_info();

    printf(">>> ALL 7 KASLR ADVANCED SECURITY VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
