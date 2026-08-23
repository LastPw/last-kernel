#ifndef GPU_ENGINE_H
#define GPU_ENGINE_H

#include "types.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  GPU Engine — Hardware-Accelerated 2D Rendering
 *
 *  Architecture:
 *    CPU builds a command buffer of drawing operations.
 *    GPU executes them in batch via DMA.
 *
 *  This replaces the CPU pixel-by-pixel rendering with GPU-native
 *  operations:
 *    - Fill rectangles (GPU memory write)
 *    - Copy/blit rectangles (GPU DMA, no CPU involvement)
 *    - Alpha blending (GPU blend unit)
 *    - Text rendering (GPU-accelerated glyph blitting)
 *    - Layer compositing (GPU multi-resource scanout)
 *
 *  With virgl (3D): uses OpenGL shaders for all rendering.
 *  Without virgl (2D): uses VirtIO-GPU resource commands.
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── GPU Command Types ── */
typedef enum {
    GPU_CMD_NOP = 0,

    /* Memory operations — GPU does the work, not CPU */
    GPU_CMD_FILL_RECT,       /* Fill a rectangle with a solid color */
    GPU_CMD_COPY_RECT,       /* Copy pixels from one region to another (DMA) */
    GPU_CMD_BLEND_RECT,      /* Alpha-blend a rectangle over existing content */
    GPU_CMD_CLEAR,           /* Clear entire surface to a color */

    /* Texture operations */
    GPU_CMD_DRAW_GLYPH,      /* Blit a pre-rasterized glyph bitmap */
    GPU_CMD_DRAW_IMAGE,      /* Blit an image (RGBA) with optional scaling */

    /* Surface management */
    GPU_CMD_CREATE_SURFACE,  /* Create an offscreen render target */
    GPU_CMD_DESTROY_SURFACE, /* Destroy a surface */
    GPU_CMD_SET_TARGET,      /* Set which surface subsequent commands render to */

    /* Presentation */
    GPU_CMD_FLUSH_SURFACE,   /* Transfer surface content to host display */
    GPU_CMD_FLIP,            /* Page flip: swap front/back surfaces */

    /* Compositing */
    GPU_CMD_COMPOSITE,       /* Blend multiple surfaces into the target */

    /* Synchronization */
    GPU_CMD_SYNC,            /* Wait for GPU to finish all pending commands */
} gpu_cmd_type_t;

/* ── Command: Fill Rectangle ── */
typedef struct {
    u32 x, y, w, h;
    u32 color;               /* ARGB8888 */
} gpu_cmd_fill_rect_t;

/* ── Command: Copy Rect (DMA) ── */
typedef struct {
    u32 src_x, src_y;
    u32 dst_x, dst_y;
    u32 w, h;
    u32 src_surface;         /* source surface ID (0 = current target) */
} gpu_cmd_copy_rect_t;

/* ── Command: Blend Rect ── */
typedef struct {
    u32 x, y, w, h;
    u32 color;               /* ARGB8888 with alpha */
    u8  opacity;             /* global opacity 0-255 */
} gpu_cmd_blend_rect_t;

/* ── Command: Draw Glyph ── */
typedef struct {
    u32 x, y;                /* screen position */
    u32 glyph_w, glyph_h;    /* glyph dimensions */
    u32 text_color;          /* ARGB8888 */
    u32 bg_color;            /* background (0 = transparent) */
    const u8* bitmap;        /* 1-bit or 8-bit glyph data */
    u8 bpp;                  /* 1 = monochrome, 8 = antialiased */
} gpu_cmd_draw_glyph_t;

/* ── Command: Draw Image ── */
typedef struct {
    u32 dst_x, dst_y;
    u32 dst_w, dst_h;
    u32 src_w, src_h;        /* original image size */
    const u32* pixels;       /* ARGB8888 pixel data */
    u8  alpha;               /* global alpha 0-255 */
} gpu_cmd_draw_image_t;

/* ── Command: Surface Operations ── */
typedef struct {
    u32 surface_id;
    u32 width, height;
} gpu_cmd_surface_t;

/* ── Command: Set Target ── */
typedef struct {
    u32 surface_id;          /* 0 = default framebuffer */
} gpu_cmd_set_target_t;

/* ── Command: Composite ── */
typedef struct {
    u32 src_surface;
    u32 dst_x, dst_y;        /* position on target */
    u8  opacity;             /* global opacity */
    u8  blend_mode;          /* 0 = normal, 1 = multiply, 2 = screen */
} gpu_cmd_composite_t;

/* ── Generic Command ── */
typedef struct __attribute__((packed)) {
    u8  type;                /* gpu_cmd_type_t */
    u8  padding[3];
    union {
        gpu_cmd_fill_rect_t      fill;
        gpu_cmd_copy_rect_t      copy;
        gpu_cmd_blend_rect_t     blend;
        gpu_cmd_draw_glyph_t     glyph;
        gpu_cmd_draw_image_t     image;
        gpu_cmd_surface_t        surface;
        gpu_cmd_set_target_t     target;
        gpu_cmd_composite_t      composite;
    } op;
} gpu_command_t;

/* ═══════════════════════════════════════════════════════════════════════
 *  API
 * ═══════════════════════════════════════════════════════════════════════ */

/* Initialize the GPU engine. Must be called after virtio_gpu_init(). */
int gpu_engine_init(void);

/* ── Command Buffer (CPU builds commands here) ── */

/* Begin a new command batch. All subsequent gpu_engine_* calls
   add commands to this batch. */
void gpu_engine_begin(void);

/* Submit the current batch to the GPU for execution. */
int gpu_engine_submit(void);

/* Wait for all pending GPU operations to complete. */
void gpu_engine_wait(void);

/* ── Drawing Commands (add to current batch) ── */

/* Fill a rectangle with a solid color. GPU-accelerated. */
void gpu_engine_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

/* Copy a rectangular region (GPU DMA, zero CPU involvement). */
void gpu_engine_copy_rect(u32 src_x, u32 src_y,
                           u32 dst_x, u32 dst_y,
                           u32 w, u32 h);

/* Alpha-blend a colored rectangle over existing content. */
void gpu_engine_blend_rect(u32 x, u32 y, u32 w, u32 h,
                            u32 color, u8 opacity);

/* Clear the entire surface to a color. */
void gpu_engine_clear(u32 color);

/* Draw a pre-rasterized glyph (text rendering helper). */
void gpu_engine_draw_glyph(u32 x, u32 y, u32 glyph_w, u32 glyph_h,
                            u32 text_color, u32 bg_color,
                            const u8* bitmap, u8 bpp);

/* Draw an image with optional scaling. */
void gpu_engine_draw_image(u32 dst_x, u32 dst_y, u32 dst_w, u32 dst_h,
                            u32 src_w, u32 src_h,
                            const u32* pixels, u8 alpha);

/* ── Surface Management ── */
u32  gpu_engine_create_surface(u32 width, u32 height);
void gpu_engine_destroy_surface(u32 surface_id);
void gpu_engine_set_target(u32 surface_id);
u32* gpu_engine_surface_pixels(u32 surface_id);
void gpu_engine_surface_size(u32 surface_id, u32* w, u32* h);

/* ── Presentation ── */
void gpu_engine_flush_surface(u32 surface_id);
void gpu_engine_flip(void);

/* ── Compositing ── */
void gpu_engine_composite(u32 src_surface, u32 dst_x, u32 dst_y,
                           u8 opacity, u8 blend_mode);

/* ── Stats ── */
u32  gpu_engine_commands_count(void);
u32  gpu_engine_surfaces_count(void);
const char* gpu_engine_name(void);

#endif /* GPU_ENGINE_H */
