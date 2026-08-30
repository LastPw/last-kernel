#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "intel_i915.h"
#include "amd_radeon.h"
#include "nvidia_nouveau.h"

/* Host Mocks */
void serial_write(const char* s) { (void)s; }
void serial_write_dec(u32 v) { (void)v; }
void serial_write_hex(u32 v) { (void)v; }
void spinlock_init(void* lock) { (void)lock; }
void spinlock_lock(void* lock) { (void)lock; }
void spinlock_unlock(void* lock) { (void)lock; }
void str_ncpy(char* dst, const char* src, u32 n) { strncpy(dst, src, n); }
int pci_enable_bus_master(const pci_device_t* dev) { (void)dev; return 0; }

/* In-Memory Mock Hardware MMIO Aperture (16MB) */
static u32 s_mock_mmio[4 * 1024 * 1024];

int pci_find_device(u16 vendor, u16 device, pci_device_t* out) {
    (void)device;
    out->vendor = vendor;
    out->device = 0x1912; /* Default Intel Skylake HD 530 */
    out->bus = 0;
    out->slot = 2;
    out->func = 0;
    out->bar0 = (u32)(uintptr_t)s_mock_mmio;
    return 0;
}

int pci_find_class(u8 class_code, u8 subclass, u8 prog_if, pci_device_t* out) {
    (void)class_code; (void)subclass; (void)prog_if;
    out->vendor = 0x8086;
    out->device = 0x1912;
    out->bus = 0;
    out->slot = 2;
    out->func = 0;
    out->bar0 = (u32)(uintptr_t)s_mock_mmio;
    return 0;
}

static void test_intel_i915_driver(void) {
    printf("  [1/3] Testing Intel i915 Driver (MSI-X / Auto-Watchdog / Free-DMA)... ");
    int res = intel_i915_init();
    assert(res == 0);
    assert(intel_i915_available() == 1);

    /* Test MSI-X */
    assert(intel_i915_enable_msix(8) == 0);

    /* Test DMA allocation and free */
    u64 dma_phys = 0;
    void* dma_virt = NULL;
    int dma_res = intel_i915_dma_alloc_coherent(131072, &dma_phys, &dma_virt);
    assert(dma_res == 0);
    assert(dma_virt != NULL);
    assert(intel_i915_dma_free_coherent(dma_virt, 131072) == 0);

    /* Test IRQ & VSYNC */
    intel_i915_handle_irq(48);
    assert(intel_i915_wait_vblank(INTEL_PIPE_A) == 0);

    /* Test Periodic Watchdog & TDR Recovery */
    intel_i915_watchdog_tick(500);
    assert(intel_i915_check_and_recover_hang(INTEL_ENGINE_BCS) == 0);
    const intel_i915_device_t* dev = intel_i915_get_device();
    assert(dev->reset_recovery_count > 0);

    int test_res = intel_i915_run_selftest();
    assert(test_res == 0);
    printf("PASS (MSI-X 8-Vec, Auto-Watchdog & DMA Bitmap Recycle Active)\n");
}

static void test_amd_radeon_driver(void) {
    printf("  [2/3] Testing AMD Radeon Driver (MSI-X / Auto-Watchdog / Free-DMA)... ");
    int res = amd_radeon_init();
    assert(res == 0);
    assert(amd_radeon_available() == 1);

    /* Test MSI-X */
    assert(amd_radeon_enable_msix(8) == 0);

    /* Test DMA allocation and free */
    u64 dma_phys = 0;
    void* dma_virt = NULL;
    int dma_res = amd_radeon_dma_alloc_coherent(131072, &dma_phys, &dma_virt);
    assert(dma_res == 0);
    assert(dma_virt != NULL);
    assert(amd_radeon_dma_free_coherent(dma_virt, 131072) == 0);

    /* Test IRQ & VSYNC */
    amd_radeon_handle_irq(50);
    assert(amd_radeon_wait_vblank(0) == 0);

    /* Test Periodic Watchdog & TDR Recovery */
    amd_radeon_watchdog_tick(500);
    assert(amd_radeon_check_and_recover_hang() == 0);
    const amd_radeon_device_t* dev = amd_radeon_get_device();
    assert(dev->reset_recovery_count > 0);

    int test_res = amd_radeon_run_selftest();
    assert(test_res == 0);
    printf("PASS (MSI-X 8-Vec, SRBM Auto-TDR & DMA Bitmap Recycle Active)\n");
}

static void test_nvidia_nouveau_driver(void) {
    printf("  [3/3] Testing NVIDIA GeForce Driver (MSI-X / Auto-Watchdog / Free-DMA)... ");
    int res = nvidia_nouveau_init();
    assert(res == 0);
    assert(nvidia_nouveau_available() == 1);

    /* Test MSI-X */
    assert(nvidia_nouveau_enable_msix(8) == 0);

    /* Test DMA allocation and free */
    u64 dma_phys = 0;
    void* dma_virt = NULL;
    int dma_res = nvidia_nouveau_dma_alloc_coherent(131072, &dma_phys, &dma_virt);
    assert(dma_res == 0);
    assert(dma_virt != NULL);
    assert(nvidia_nouveau_dma_free_coherent(dma_virt, 131072) == 0);

    /* Test IRQ & VSYNC */
    nvidia_nouveau_handle_irq(52);
    assert(nvidia_nouveau_wait_vblank(0) == 0);

    /* Test Periodic Watchdog & TDR Recovery */
    nvidia_nouveau_watchdog_tick(500);
    assert(nvidia_nouveau_check_and_recover_hang() == 0);
    const nvidia_device_t* dev = nvidia_nouveau_get_device();
    assert(dev->reset_recovery_count > 0);

    int test_res = nvidia_nouveau_run_selftest();
    assert(test_res == 0);
    printf("PASS (MSI-X 8-Vec, PMC Auto-TDR & DMA Bitmap Recycle Active)\n");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   LastKernel Industrial GPU Hardware Drivers Tests    \n");
    printf("=======================================================\n");
    test_intel_i915_driver();
    test_amd_radeon_driver();
    test_nvidia_nouveau_driver();
    printf(">>> ALL 3 GPU ARCHITECTURE DRIVERS PASSED 100%% SUCCESSFULLY! <<<\n\n");
    return 0;
}
