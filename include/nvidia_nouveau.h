#ifndef NVIDIA_NOUVEAU_H
#define NVIDIA_NOUVEAU_H

#include "types.h"
#include "pci.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── NVIDIA GPU PCI Vendor & Microarchitecture Identification ───────────── */
#define NVIDIA_PCI_VENDOR_ID            0x10DE

typedef enum {
    NV_ARCH_UNKNOWN = 0,
    NV_ARCH_KEPLER,     /* GK104 / GK110 (GTX 680 / GTX 780 Ti) */
    NV_ARCH_MAXWELL,    /* GM107 / GM204 (GTX 750 Ti / GTX 970 / GTX 980) */
    NV_ARCH_PASCAL,     /* GP102 / GP104 / GP106 (GTX 1060 / GTX 1080 Ti) */
    NV_ARCH_VOLTA,      /* GV100 (Titan V) */
    NV_ARCH_TURING,     /* TU102 / TU104 / TU106 (RTX 2060 / RTX 2080 Ti / GTX 1660) */
    NV_ARCH_AMPERE,     /* GA102 / GA104 / GA106 (RTX 3060 / RTX 3080 / RTX 3090) */
    NV_ARCH_ADA_LOVELACE/* AD102 / AD103 / AD104 (RTX 4070 / RTX 4080 / RTX 4090) */
} nv_gpu_arch_t;

/* ─── NVIDIA FIFO Channel Subchannel Engine Classes ──────────────────────── */
#define NV_SUBCH_2D                     0
#define NV_SUBCH_3D                     1
#define NV_SUBCH_COMPUTE                2
#define NV_SUBCH_COPY                   3
#define NV_SUBCH_DECODE                 4

/* ─── NVIDIA Display Timing Structure ────────────────────────────────────── */
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
    u8  g_sync_enabled;
} nv_display_mode_t;

/* ─── NVIDIA Hardware Context ────────────────────────────────────────────── */
typedef struct {
    u8                  is_detected;
    u8                  is_initialized;
    pci_device_t        pci_dev;
    nv_gpu_arch_t       arch;
    char                device_name[48];

    /* MMIO Register Space */
    u64                 mmio_phys_base;
    volatile u32*       mmio_base;
    u32                 mmio_size;

    /* VRAM Direct Memory Aperture */
    u64                 vram_phys_base;
    u32*                vram_virtual_base;
    u64                 vram_total_bytes;

    /* GMMU Virtual Memory (49-Bit 4-Level Page Tables) */
    u64                 gmmu_pd0_phys_addr;
    u32                 gmmu_total_pages;

    /* Host1X / PFIFO PushBuffer */
    u64                 pushbuffer_phys_addr;
    volatile u32*       pushbuffer_virtual;
    u32                 pushbuffer_size_bytes;
    u32                 put;
    u32                 get;
    u32                 subchannel_class[8];

    /* Display State */
    nv_display_mode_t   active_mode;

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
    u32                 last_known_get;

    /* Coherent Physical DMA Pool (<4GB) */
    u64                 dma_coherent_allocated_bytes;
    u64                 dma_coherent_freed_bytes;

    /* Telemetry */
    u64                 total_methods_executed;
    u32                 core_clock_mhz;
    u32                 memory_clock_mhz;
    u32                 temperature_celsius;
} nvidia_device_t;

/* ─── Public API ─────────────────────────────────────────────────────────── */
int  nvidia_nouveau_init(void);
int  nvidia_nouveau_available(void);
const nvidia_device_t* nvidia_nouveau_get_device(void);

/* Interrupt, MSI-X & VBlank Synchronization */
void nvidia_nouveau_handle_irq(u8 irq_vector);
int  nvidia_nouveau_enable_msix(u32 vector_count);
int  nvidia_nouveau_wait_vblank(u8 head);

/* GPU Hang Detection & Automatic Periodic Watchdog */
int  nvidia_nouveau_check_and_recover_hang(void);
void nvidia_nouveau_watchdog_tick(u32 delta_ms);

/* Coherent DMA Memory Allocation & Free */
int  nvidia_nouveau_dma_alloc_coherent(u32 size, u64* out_phys, void** out_virt);
int  nvidia_nouveau_dma_free_coherent(void* virt, u32 size);

/* Display & NVDISP Mode Setting */
int  nvidia_nouveau_set_mode(u8 head, const nv_display_mode_t* mode, u64 vram_fb_offset);
int  nvidia_nouveau_page_flip(u8 head, u64 new_vram_fb_offset);

/* Hardware 2D Acceleration & Blit */
int  nvidia_nouveau_push_methods(u8 subchannel, u32 method, const u32* data, u32 count);
int  nvidia_nouveau_2d_fill_rect(u64 dst_vram_addr, u32 dst_pitch, u32 x, u32 y, u32 w, u32 h, u32 color);
int  nvidia_nouveau_2d_copy_rect(u64 dst_vram_addr, u32 dst_pitch, u64 src_vram_addr, u32 src_pitch,
                                 u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 w, u32 h);

/* Power & Diagnostics */
void nvidia_nouveau_get_status(void);
void nvidia_nouveau_dump_telemetry(void);
int  nvidia_nouveau_run_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* NVIDIA_NOUVEAU_H */
