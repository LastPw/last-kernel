#ifndef GPU_BACKEND_H
#define GPU_BACKEND_H

#include "types.h"

/* ── GPU Backend Abstraction ──
 *
 * This layer sits between the gfx_* API and the actual hardware.
 * When VirtIO-GPU is available, the backend routes rendering to the
 * GPU framebuffer. Otherwise, it falls back to the CPU framebuffer.
 *
 * The key difference:
 *   CPU backend: writes to s_backbuffer, copies to VRAM on present
 *   GPU backend: writes directly to GPU framebuffer memory, flushes via VirtIO-GPU
 */

typedef enum {
    GPU_BACKEND_NONE = 0,    /* No graphics */
    GPU_BACKEND_CPU  = 1,    /* CPU framebuffer (BGA, Limine) */
    GPU_BACKEND_GPU  = 2,    /* VirtIO-GPU (hardware scanout) */
} gpu_backend_type_t;

/* Initialize the GPU backend. Probes VirtIO-GPU first, falls back to CPU.
   Returns the backend type that was selected. */
gpu_backend_type_t gpu_backend_init(void);

/* Returns the current backend type. */
gpu_backend_type_t gpu_backend_type(void);

/* Returns 1 if a GPU backend (VirtIO-GPU) is active. */
int gpu_backend_is_gpu(void);

/* Returns a human-readable name for the active backend. */
const char* gpu_backend_name(void);

/* ── Framebuffer Access ── */

/* Returns a pointer to the writable framebuffer memory.
   For GPU backend: this is the VirtIO-GPU framebuffer in guest RAM.
   For CPU backend: this is s_backbuffer.
   Writing to this pointer updates the visible image (after flush). */
u32* gpu_backend_framebuffer(void);

/* Returns the framebuffer width in pixels. */
u32 gpu_backend_width(void);

/* Returns the framebuffer height in pixels. */
u32 gpu_backend_height(void);

/* Returns the framebuffer pitch (bytes per row). */
u32 gpu_backend_pitch(void);

/* Returns bytes per pixel (always 4 for XRGB8888). */
u32 gpu_backend_bpp(void);

/* Returns total framebuffer size in bytes. */
u32 gpu_backend_fb_size(void);

/* ── Damage Tracking & Presentation ── */

/* Mark a rectangular region as dirty (needs flushing to display). */
void gpu_backend_damage_add(u32 x, u32 y, u32 w, u32 h);

/* Mark the entire screen as dirty. */
void gpu_backend_damage_add_full(void);

/* Reset the damage tracker (call after flush). */
void gpu_backend_damage_reset(void);

/* Flush the damaged region to the display.
   GPU backend: calls VirtIO-GPU TRANSFER + FLUSH.
   CPU backend: copies backbuffer to VRAM. */
void gpu_backend_present_damaged(void);

/* Force a full-screen flush immediately. */
void gpu_backend_present_full(void);

/* Returns 1 if there are pending dirty regions. */
int gpu_backend_has_damage(void);

/* Flush a specific rectangular region directly to the display.
   Unlike present_damaged(), this bypasses the damage tracker and
   flushes the exact region specified. */
void gpu_backend_flush_rect(u32 x, u32 y, u32 w, u32 h);

/* Page flip: swap front and back buffers (GPU backend only).
   Returns 0 on success, -1 if not supported. */
int gpu_backend_page_flip(void);

#endif /* GPU_BACKEND_H */
