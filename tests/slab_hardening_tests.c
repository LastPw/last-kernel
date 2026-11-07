/*
 * =============================================================================
 * LastKernel — Advanced Kernel Security Subsystem
 * File: tests/slab_hardening_tests.c
 * Description: Automated Verification Matrix for SLUB Freelist Hardening,
 *              XOR Pointer Obfuscation, and Double-Free Prevention.
 * Author: Taha Vaziry
 * =============================================================================
 */

#define HOST_TEST 1

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef NULL
#undef NULL
#endif
#ifdef offsetof
#undef offsetof
#endif

#include "mm/slab.h"
#include "security/kaslr.h"

/* ── Host Mocks for Slab Allocator ── */

void* frame_alloc(void) {
    void* p = NULL;
#if defined(_WIN32)
    p = _aligned_malloc(4096, 4096);
#else
    if (posix_memalign(&p, 4096, 4096) != 0) return NULL;
#endif
    if (p) memset(p, 0, 4096);
    return p;
}

void frame_free(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

void* frame_alloc_order(uint8_t order) {
    size_t sz = 4096ULL << order;
    void* p = NULL;
#if defined(_WIN32)
    p = _aligned_malloc(sz, 4096);
#else
    if (posix_memalign(&p, 4096, sz) != 0) return NULL;
#endif
    return p;
}

void frame_free_order(void* p, uint8_t order) {
    (void)order;
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

uint64_t paging_phys_to_virt(uint64_t phys) { return phys; }
uint64_t paging_virt_to_phys(uint64_t virt) { return virt; }

void serial_writeln(const char* s) { (void)s; }
void serial_write(const char* s) { (void)s; }
void serial_write_hex(uint32_t val) { (void)val; }
void serial_write_dec(uint32_t val) { (void)val; }

void rust_bridge_mem_on_alloc(uint64_t sz) { (void)sz; }
void rust_bridge_mem_on_free(uint64_t sz) { (void)sz; }

u32 str_lcpy(char* dst, const char* src, u32 size) {
    if (!dst || !src || size == 0) return 0;
    u32 i = 0;
    while (src[i] && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    while (src[i]) i++;
    return i;
}

u32 str_lcat(char* dst, const char* src, u32 size) {
    if (!dst || !src || size == 0) return 0;
    u32 dlen = 0;
    while (dst[dlen] && dlen < size) dlen++;
    if (dlen >= size) return dlen + (u32)strlen(src);
    u32 i = 0;
    while (src[i] && dlen + i + 1 < size) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
    return dlen + (u32)strlen(src);
}

/* ── Test 1: Freelist Pointer Obfuscation Verification ── */
static void test_freelist_pointer_obfuscation(void) {
    printf("  [1/5] Testing SLUB Freelist XOR Pointer Obfuscation... ");

    kmem_cache_t* cache = kmem_cache_create("sec_test_32", 32, 8, 0);
    assert(cache != NULL);

    /* Allocate an object */
    void* p1 = kmem_cache_alloc(cache);
    assert(p1 != NULL);
    assert(((uintptr_t)p1 & 7) == 0);

    /* Free it back to the hardened freelist */
    kmem_cache_free(cache, p1);

    /* Inspect the memory at p1: in plain slab, it would store a raw pointer to next (NULL/prev).
       In hardened slab, it is scrambled with (p ^ obj_addr ^ cookie). */
    uintptr_t* raw_mem = (uintptr_t*)p1;
    /* It must NOT be a plaintext NULL (0x0) or plaintext kernel address */
    assert(*raw_mem != 0);

    /* Allocate it again and verify it is successfully decoded */
    void* p2 = kmem_cache_alloc(cache);
    assert(p2 == p1);

    kmem_cache_free(cache, p2);
    kmem_cache_destroy(cache);
    printf("PASS (Raw pointer scrambled with (p ^ addr ^ cookie))\n");
}

/* ── Test 2: Double-Free Detection and Prevention ── */
static void test_double_free_detection(void) {
    printf("  [2/5] Testing Immediate Double-Free Detection & Interception... ");

    kmem_cache_t* cache = kmem_cache_create("sec_df_test", 64, 8, 0);
    assert(cache != NULL);

    void* p = kmem_cache_alloc(cache);
    assert(p != NULL);

    /* First free: valid */
    kmem_cache_free(cache, p);

    u32 corruptions = 0, double_frees_before = 0;
    slab_hardening_get_stats(&corruptions, &double_frees_before);

    /* Second free of the same pointer: MUST be detected and ignored */
    kmem_cache_free(cache, p);

    u32 double_frees_after = 0;
    slab_hardening_get_stats(&corruptions, &double_frees_after);

    assert(double_frees_after == double_frees_before + 1);

    /* Ensure cache remains functional and doesn't get corrupted */
    void* p_realloc = kmem_cache_alloc(cache);
    assert(p_realloc == p);

    kmem_cache_free(cache, p_realloc);
    kmem_cache_destroy(cache);
    printf("PASS (Double-free intercepted without corruption)\n");
}

/* ── Test 3: Heap Metadata Corruption Detection ── */
static void test_heap_corruption_detection(void) {
    printf("  [3/5] Testing Out-of-Bounds Heap Freelist Overwrite Mitigation... ");

    kmem_cache_t* cache = kmem_cache_create("sec_corrupt_test", 32, 8, 0);
    assert(cache != NULL);

    void* a = kmem_cache_alloc(cache);
    void* b = kmem_cache_alloc(cache);
    assert(a != NULL && b != NULL);

    kmem_cache_free(cache, a);

    u32 corruptions_before = 0, double_frees = 0;
    slab_hardening_get_stats(&corruptions_before, &double_frees);

    /* Simulate attacker writing malicious forged pointer into free chunk 'a' */
    uintptr_t* forge = (uintptr_t*)a;
    *forge = 0xDEADBEEFCAFEBABEULL; /* Wild pointer target */

    /* The allocator attempts to pop 'a' and decode the forged next pointer:
       it immediately detects that next_encoded is outside the slab page,
       blocks the corrupted allocation from succeeding, and traps the exploit! */
    void* popped = kmem_cache_alloc(cache);
    assert(popped == NULL); /* Trapped immediately at the head */

    u32 corruptions_after = 0;
    slab_hardening_get_stats(&corruptions_after, &double_frees);
    assert(corruptions_after >= corruptions_before + 1);

    kmem_cache_free(cache, b);
    kmem_cache_destroy(cache);
    printf("PASS (Forged freelist pointer trapped and rejected)\n");
}

/* ── Test 4: General kmalloc / kzalloc / kfree Integration ── */
static void test_kmalloc_kzalloc_kfree(void) {
    printf("  [4/5] Testing kmalloc, kzalloc & kfree with Multi-Cache Pool... ");

    slab_init();

    void* ptrs[8];
    size_t sizes[8] = { 12, 28, 55, 110, 240, 500, 1000, 1800 };

    for (int i = 0; i < 8; i++) {
        ptrs[i] = kzalloc(sizes[i]);
        assert(ptrs[i] != NULL);
        /* Check zeroed */
        uint8_t* b = (uint8_t*)ptrs[i];
        for (size_t j = 0; j < sizes[i]; j++) {
            assert(b[j] == 0);
        }
        /* Write marker */
        memset(ptrs[i], 0xAA, sizes[i]);
    }

    for (int i = 0; i < 8; i++) {
        kfree(ptrs[i]);
    }

    printf("PASS (All general size classes verified)\n");
}

/* ── Test 5: Slab Hardening Telemetry & Statistics ── */
static void test_slab_telemetry(void) {
    printf("  [5/5] Testing SLUB Hardening Telemetry & Diagnostics... ");

    u32 corruptions = 0, double_frees = 0;
    slab_hardening_get_stats(&corruptions, &double_frees);
    assert(double_frees >= 1);
    assert(corruptions >= 1);

    printf("PASS (Trapped %u corruptions, %u double-frees)\n", corruptions, double_frees);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("\n=================================================================\n");
    printf("   LastKernel SLUB Freelist Hardening & Exploit Mitigation Suite \n");
    printf("=================================================================\n");

    test_freelist_pointer_obfuscation();
    test_double_free_detection();
    test_heap_corruption_detection();
    test_kmalloc_kzalloc_kfree();
    test_slab_telemetry();

    printf("\n>>> ALL 5 SLUB FREELIST HARDENING VECTORS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
