#ifndef AMD_RADEON_H
#define AMD_RADEON_H

#include "types.h"
#include "pci.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── AMD GPU PCI Vendor & Architecture Identification ───────────────────── */
#define AMD_PCI_VENDOR_ID               0x1002

typedef enum {
    AMD_ARCH_UNKNOWN = 0,
    AMD_ARCH_GCN1_SOUTHERN_ISLANDS, /* HD 7000 / R9 280 (Tahiti/Pitcairn) */
    AMD_ARCH_GCN2_SEA_ISLANDS,      /* R9 290 / R9 390 (Hawaii/Bonaire) */
    AMD_ARCH_GCN3_VOLCANIC_ISLANDS, /* R9 Fury / R9 380 (Fiji/Tonga) */
    AMD_ARCH_GCN4_POLARIS,          /* RX 480 / RX 580 / RX 590 (Polaris 10/20) */
    AMD_ARCH_GCN5_VEGA,             /* Vega 56 / 64 / Radeon VII (Vega 10/20) */
    AMD_ARCH_RDNA1_NAVI1X,          /* RX 5700 XT (Navi 10) */
    AMD_ARCH_RDNA2_NAVI2X,          /* RX 6800 / 6900 XT (Navi 21) */
    AMD_ARCH_RDNA3_NAVI3X,          /* RX 7900 XTX / 7800 XT (Navi 31/32) */
} amd_gpu_arch_t;

/* ─── AMD Command Processor (CP) Ring Identifiers ────────────────────────── */
typedef enum {
    AMD_RING_GFX = 0,               /* 3D Graphics Ring */
    AMD_RING_COMPUTE_0,             /* Async Compute Ring 0 */
    AMD_RING_COMPUTE_1,             /* Async Compute Ring 1 */
    AMD_RING_SDMA_0,                /* System DMA Copy Engine 0 */
    AMD_RING_SDMA_1,                /* System DMA Copy Engine 1 */
    AMD_RING_VCN_DEC,               /* Video Core Next Decode */
    AMD_RING_VCN_ENC,               /* Video Core Next Encode */
    AMD_RING_MAX_COUNT
} amd_ring_id_t;

/* ─── AMD Display Timing Structure ───────────────────────────────────────── */
typedef struct {
    u32 pixel_clock_khz;
    u16 hactive;
    u16 hsync_start;
    u16 hsync_end;
    u16 htotal;
    u16 vactive;
    u16 vsync_start;
    u16 vsync_end;
    u16 vtotal;
    u8  hsync_positive;
    u8  vsync_positive;
    u8  vrr_freesync_enabled;
} amd_display_mode_t;

/* ─── AMD Radeon Hardware Context ────────────────────────────────────────── */
typedef struct {
    u8                  is_detected;
    u8                  is_initialized;
    pci_device_t        pci_dev;
    amd_gpu_arch_t      arch;
    char                device_name[48];

    /* MMIO Register Aperture */
    u64                 mmio_phys_base;
    volatile u32*       mmio_base;
    u32                 mmio_size;

    /* VRAM Direct Memory Aperture */
    u64                 vram_phys_base;
    u32*                vram_virtual_base;
    u64                 vram_total_bytes;
    u64                 vram_visible_bytes;

    /* GPU Virtual Memory (VM) Page Tables */
    u64                 vm_pd0_phys_addr;
    u32                 vm_total_pages;

    /* CP Ring Buffers */
    struct {
        u64             ring_phys_addr;
        volatile u32*   ring_virtual;
        u32             ring_size_bytes;
        u32             wptr;
        u32             rptr;
        u32             space_free;
    } rings[AMD_RING_MAX_COUNT];

    /* Display State */
    amd_display_mode_t  current_mode;

    /* Interrupt & VSYNC Handling */
    u8                  irq_enabled;
    u8                  irq_line;
    u8                  msix_enabled;
    u32                 msix_vectors_count;
    u64                 vblank_counter[4];
    u64                 fence_seqno;

    /* GPU Hang & TDR Automatic Periodic Watchdog */
    u32                 hang_detected_count;
    u32                 reset_recovery_count;
    u32                 last_progress_timestamp_ms;
    u32                 last_known_rptr;

    /* Coherent Physical DMA Pool (<4GB) */
    u64                 dma_coherent_allocated_bytes;
    u64                 dma_coherent_freed_bytes;

    /* Telemetry */
    u64                 total_packets_submitted;
    u64                 total_dma_bytes_transferred;
    u32                 sclk_mhz;   /* Engine / Shader Clock */
    u32                 mclk_mhz;   /* Memory VRAM Clock */
    u32                 temperature_celsius;
} amd_radeon_device_t;

/* ─── Public API ─────────────────────────────────────────────────────────── */
int  amd_radeon_init(void);
int  amd_radeon_available(void);
const amd_radeon_device_t* amd_radeon_get_device(void);

/* Interrupt, MSI-X & VBlank Synchronization */
void amd_radeon_handle_irq(u8 irq_vector);
int  amd_radeon_enable_msix(u32 vector_count);
int  amd_radeon_wait_vblank(u8 pipe);

/* GPU Hang Detection & Automatic Periodic Watchdog */
int  amd_radeon_check_and_recover_hang(void);
void amd_radeon_watchdog_tick(u32 delta_ms);

/* Coherent DMA Memory Allocation & Free */
int  amd_radeon_dma_alloc_coherent(u32 size, u64* out_phys, void** out_virt);
int  amd_radeon_dma_free_coherent(void* virt, u32 size);

/* Display & DCN Mode Setting */
int  amd_radeon_set_mode(u8 pipe, const amd_display_mode_t* mode, u64 vram_fb_offset);
int  amd_radeon_page_flip(u8 pipe, u64 new_vram_fb_offset);
void amd_radeon_set_freesync(u8 enable, u16 min_hz, u16 max_hz);

/* Hardware Acceleration & SDMA Blit */
int  amd_radeon_submit_pm4(amd_ring_id_t ring, const u32* packets, u32 dword_count);
int  amd_radeon_sdma_fill_rect(u64 dst_vram_addr, u32 dst_pitch, u32 x, u32 y, u32 w, u32 h, u32 color);
int  amd_radeon_sdma_copy_rect(u64 dst_vram_addr, u32 dst_pitch, u64 src_vram_addr, u32 src_pitch,
                               u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 w, u32 h);

/* Power & Telemetry */
int  amd_radeon_set_power_profile(u8 dpm_level);
void amd_radeon_get_status(void);
void amd_radeon_dump_telemetry(void);
int  amd_radeon_run_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* AMD_RADEON_H */
