#ifndef INTEL_I915_H
#define INTEL_I915_H

#include "types.h"
#include "pci.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Intel GPU PCI Vendor & Generation Identification ───────────────────── */
#define INTEL_PCI_VENDOR_ID             0x8086

typedef enum {
    INTEL_GEN_UNKNOWN = 0,
    INTEL_GEN7_IVYBRIDGE,
    INTEL_GEN7_5_HASWELL,
    INTEL_GEN8_BROADWELL,
    INTEL_GEN9_SKYLAKE,
    INTEL_GEN9_KABYLAKE,
    INTEL_GEN9_COFFEELAKE,
    INTEL_GEN11_ICELAKE,
    INTEL_GEN12_TIGERLAKE,
    INTEL_GEN12_ALDERLAKE,
    INTEL_GEN12_RAPTORLAKE,
    INTEL_XE_HPG_ARC_ALCHEMIST,
} intel_gpu_gen_t;

/* ─── Intel Display Pipes, Planes & Transcoders ──────────────────────────── */
typedef enum {
    INTEL_PIPE_A = 0,
    INTEL_PIPE_B = 1,
    INTEL_PIPE_C = 2,
    INTEL_PIPE_D = 3,
} intel_pipe_t;

typedef enum {
    INTEL_PLANE_PRIMARY = 0,
    INTEL_PLANE_SPRITE1 = 1,
    INTEL_PLANE_SPRITE2 = 2,
    INTEL_PLANE_CURSOR  = 3,
} intel_plane_t;

typedef enum {
    INTEL_PORT_A = 0, /* eDP */
    INTEL_PORT_B = 1, /* DP/HDMI */
    INTEL_PORT_C = 2, /* DP/HDMI */
    INTEL_PORT_D = 3, /* DP/HDMI */
    INTEL_PORT_E = 4, /* CRT / DVI */
} intel_port_t;

/* ─── Intel Command Streamer Engine Rings ────────────────────────────────── */
typedef enum {
    INTEL_ENGINE_RCS = 0, /* Render / 3D Compute Engine */
    INTEL_ENGINE_BCS = 1, /* Blitter / 2D DMA Copy Engine */
    INTEL_ENGINE_VCS0= 2, /* Video Codec Engine 0 (NVDEC/QuickSync) */
    INTEL_ENGINE_VCS1= 3, /* Video Codec Engine 1 */
    INTEL_ENGINE_VECS= 4, /* Video Enhancement Engine */
    INTEL_ENGINE_CCS0= 5, /* Compute Command Streamer 0 (Xe-HPG) */
    INTEL_ENGINE_MAX = 6,
} intel_engine_id_t;

/* ─── Display Timings Descriptor ─────────────────────────────────────────── */
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
    u8  interlaced;
} intel_display_mode_t;

/* ─── Intel GPU Hardware Subsystem State ─────────────────────────────────── */
typedef struct {
    u8                  is_detected;
    u8                  is_initialized;
    pci_device_t        pci_dev;
    intel_gpu_gen_t     gen;
    char                device_name[48];

    /* MMIO Register Space */
    u64                 mmio_phys_base;
    volatile u32*       mmio_base;
    u32                 mmio_size;

    /* Global Graphics Translation Table (GGTT) */
    u64                 ggtt_phys_base;
    volatile u64*       ggtt_entries;
    u32                 ggtt_total_entries;

    /* Stolen Memory / Framebuffer Aperture */
    u64                 stolen_phys_base;
    u64                 stolen_size_bytes;
    u64                 fb_phys_addr;
    u32*                fb_virtual_addr;
    u32                 fb_width;
    u32                 fb_height;
    u32                 fb_pitch;
    u32                 fb_bpp;

    /* Display State */
    u8                  pipe_enabled[4];
    intel_display_mode_t active_mode;

    /* Ring Buffers */
    struct {
        u64             ring_phys_addr;
        volatile u32*   ring_virtual;
        u32             ring_size_bytes;
        u32             head;
        u32             tail;
        u32             space;
    } engines[INTEL_ENGINE_MAX];

    /* Interrupt & VSYNC Handling */
    u8                  irq_enabled;
    u8                  irq_line;
    u8                  msix_enabled;
    u32                 msix_vectors_count;
    u64                 vblank_counter[4];
    u64                 user_fence_seqno;

    /* GPU Hang & TDR Automatic Periodic Watchdog */
    u32                 hang_detected_count;
    u32                 reset_recovery_count;
    u32                 engine_watchdog_cycles;
    u32                 last_progress_timestamp_ms;
    u32                 last_known_head;

    /* Coherent Physical DMA Pool (<4GB) */
    u64                 dma_coherent_allocated_bytes;
    u64                 dma_coherent_freed_bytes;

    /* Telemetry Metrics */
    u64                 total_batches_submitted;
    u64                 total_flips_completed;
    u32                 current_cdclk_khz;
    u32                 current_gpu_freq_mhz;
    u32                 temperature_celsius;
} intel_i915_device_t;

/* ─── Public API ─────────────────────────────────────────────────────────── */
int  intel_i915_init(void);
int  intel_i915_available(void);
const intel_i915_device_t* intel_i915_get_device(void);

/* Interrupt, MSI-X & VBlank Synchronization */
void intel_i915_handle_irq(u8 irq_vector);
int  intel_i915_enable_msix(u32 vector_count);
int  intel_i915_wait_vblank(intel_pipe_t pipe);

/* GPU Hang Detection & Automatic Periodic Watchdog */
int  intel_i915_check_and_recover_hang(intel_engine_id_t engine);
void intel_i915_watchdog_tick(u32 delta_ms);

/* Coherent DMA Memory Allocation & Free */
int  intel_i915_dma_alloc_coherent(u32 size, u64* out_phys, void** out_virt);
int  intel_i915_dma_free_coherent(void* virt, u32 size);

/* Display & Mode Setting */
int  intel_i915_set_mode(intel_pipe_t pipe, const intel_display_mode_t* mode, u64 fb_ggtt_offset);
int  intel_i915_page_flip(intel_pipe_t pipe, u64 new_fb_ggtt_offset);
void intel_i915_enable_pipe(intel_pipe_t pipe, u8 enable);
void intel_i915_set_backlight(u8 brightness_pct);

/* Hardware Acceleration & Blitter Execution */
int  intel_i915_submit_batch(intel_engine_id_t engine, const u32* commands, u32 dwords_count);
int  intel_i915_blt_fill_rect(u64 dst_offset, u32 dst_pitch, u32 x, u32 y, u32 w, u32 h, u32 color);
int  intel_i915_blt_copy_rect(u64 dst_offset, u32 dst_pitch, u64 src_offset, u32 src_pitch,
                              u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 w, u32 h);

/* Power & Telemetry */
int  intel_i915_set_frequency(u32 target_freq_mhz);
void intel_i915_get_status(void);
void intel_i915_dump_telemetry(void);
int  intel_i915_run_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* INTEL_I915_H */
