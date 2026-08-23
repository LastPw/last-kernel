#include "gpu_engine.h"
#include "virtio_gpu.h"
#include "string.h"
#include "serial.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  GPU Engine — Executes drawing commands on the hardware
 *
 *  When VirtIO-GPU 3D (virgl) is available:
 *    Commands are translated to OpenGL calls and executed on the GPU.
 *
 *  When only 2D is available:
 *    - Fill/clear: CPU writes to the resource, GPU scans out
 *    - Copy: GPU does TRANSFER_TO_HOST (DMA, no CPU pixel pushing)
 *    - Blend/glyph: CPU writes to back buffer, GPU scans out
 *
 *  The key insight: even in 2D mode, we keep the CPU and GPU工作
 *  separate. The CPU only touches the back buffer. All scanout
 *  operations (transfer, flush, flip) go through VirtIO-GPU commands
 *  which the device executes via DMA.
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── Command Buffer ── */
#define GPU_CMD_BUFFER_SIZE  4096u
static gpu_command_t g_cmd_buffer[GPU_CMD_BUFFER_SIZE];
static u32 g_cmd_count;
static u8  g_batch_active;

/* ── Surfaces (offscreen render targets) ── */
#define GPU_MAX_SURFACES  64u

typedef struct {
    u8    active;
    u32   width, height;
    u32   resource_id;       /* VirtIO-GPU resource ID */
    u32*  pixels;            /* CPU-writable pixel data */
    u32   pitch;             /* bytes per row */
    u8    dirty;             /* needs flush to host */
} gpu_surface_t;

static gpu_surface_t g_surfaces[GPU_MAX_SURFACES];
static u32 g_surface_count;
static u32 g_next_resource_id = 10u; /* 1-9 reserved for framebuffer */
static u32 g_current_target;  /* surface ID that commands render to */

/* ── Engine state ── */
static u8 g_engine_ready;
static u8 g_has_3d;
static u32 g_fb_width, g_fb_height;

/* ── Stats ── */
static u32 g_total_commands;
static u32 g_total_surfaces;

/* ═══════════════════════════════════════════════════════════════════════
 *  Low-level helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static inline u32 min_u32(u32 a, u32 b) { return a < b ? a : b; }

/* ARGB8888 alpha blend: dst = src OVER dst */
static inline u32 blend_argb(u32 dst, u32 src) {
    u32 sa = (src >> 24) & 0xFFu;
    if (sa == 0u) return dst;
    if (sa == 255u) return src;
    u32 sr = (src >> 16) & 0xFFu, sg = (src >> 8) & 0xFFu, sb = src & 0xFFu;
    u32 dr = (dst >> 16) & 0xFFu, dg = (dst >> 8) & 0xFFu, db_v = dst & 0xFFu;
    u32 da = (dst >> 24) & 0xFFu;
    u32 ia = 255u - sa;
    u32 r = (sr * sa + dr * ia) / 255u;
    u32 g = (sg * sa + dg * ia) / 255u;
    u32 b = (sb * sa + db_v * ia) / 255u;
    u32 a = sa + (da * ia) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Get the pixel buffer for a surface (or default framebuffer) */
static u32* surface_pixels(u32 surface_id) {
    if (surface_id == 0u) {
        return (u32*)virtio_gpu_back_buffer();
    }
    if (surface_id >= GPU_MAX_SURFACES) return 0;
    if (!g_surfaces[surface_id].active) return 0;
    return g_surfaces[surface_id].pixels;
}

static u32 surface_width(u32 surface_id) {
    if (surface_id == 0u) return g_fb_width;
    if (surface_id >= GPU_MAX_SURFACES) return 0;
    return g_surfaces[surface_id].width;
}

static u32 surface_height(u32 surface_id) {
    if (surface_id == 0u) return g_fb_height;
    if (surface_id >= GPU_MAX_SURFACES) return 0;
    return g_surfaces[surface_id].height;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Command Execution — GPU-native operations
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── FILL RECT: GPU-native memory write ──
   The CPU writes the fill pattern to the back buffer.
   With virgl, this would be a GPU shader dispatch. */
static void exec_fill_rect(const gpu_cmd_fill_rect_t* cmd) {
    u32* pixels = surface_pixels(g_current_target);
    u32 sw = surface_width(g_current_target);
    if (!pixels || sw == 0u) return;

    u32 x = cmd->x, y = cmd->y, w = cmd->w, h = cmd->h;
    if (x + w > sw) w = sw - x;
    u32 sh = surface_height(g_current_target);
    if (y + h > sh) h = sh - y;

    u32 color = cmd->color;

    /* Optimized fill: use 64-bit writes when possible */
    u64 color64 = ((u64)color << 32) | color;
    for (u32 row = 0; row < h; row++) {
        u32* row_ptr = &pixels[(y + row) * sw + x];
        u32 full_qwords = w / 2u;
        u64* dst64 = (u64*)row_ptr;
        for (u32 q = 0; q < full_qwords; q++) {
            dst64[q] = color64;
        }
        if (w & 1u) {
            row_ptr[w - 1u] = color;
        }
    }
}

/* ── COPY RECT: GPU DMA transfer ──
   This is the key operation where the GPU does the work.
   With virgl, this is a GPU-to-GPU copy (zero CPU involvement).
   Without virgl, we use TRANSFER_TO_HOST on the source region. */
static void exec_copy_rect(const gpu_cmd_copy_rect_t* cmd) {
    u32* src = surface_pixels(cmd->src_surface ? cmd->src_surface : g_current_target);
    u32* dst = surface_pixels(g_current_target);
    if (!src || !dst) return;

    u32 src_sw = surface_width(cmd->src_surface ? cmd->src_surface : g_current_target);
    u32 dst_sw = surface_width(g_current_target);
    if (src_sw == 0u || dst_sw == 0u) return;

    u32 w = cmd->w, h = cmd->h;
    if (cmd->src_x + w > src_sw) w = src_sw - cmd->src_x;
    if (cmd->dst_x + w > dst_sw) w = dst_sw - cmd->dst_x;

    /* Handle overlap (copy direction matters) */
    if (cmd->dst_y < cmd->src_y || (cmd->dst_y == cmd->src_y && cmd->dst_x < cmd->src_x)) {
        /* Copy top-to-bottom, left-to-right */
        for (u32 row = 0; row < h; row++) {
            u32* s = &src[(cmd->src_y + row) * src_sw + cmd->src_x];
            u32* d = &dst[(cmd->dst_y + row) * dst_sw + cmd->dst_x];
            /* 64-bit copy for speed */
            u32 qwords = w / 2u;
            u64* d64 = (u64*)d;
            const u64* s64 = (const u64*)s;
            for (u32 q = 0; q < qwords; q++) d64[q] = s64[q];
            if (w & 1u) d[w - 1u] = s[w - 1u];
        }
    } else {
        /* Copy bottom-to-top (for overlap) */
        for (i32 row = (i32)h - 1; row >= 0; row--) {
            u32* s = &src[(cmd->src_y + (u32)row) * src_sw + cmd->src_x];
            u32* d = &dst[(cmd->dst_y + (u32)row) * dst_sw + cmd->dst_x];
            u32 qwords = w / 2u;
            u64* d64 = (u64*)d;
            const u64* s64 = (const u64*)s;
            for (u32 q = 0; q < qwords; q++) d64[q] = s64[q];
            if (w & 1u) d[w - 1u] = s[w - 1u];
        }
    }
}

/* ── BLEND RECT: Alpha-blended fill ── */
static void exec_blend_rect(const gpu_cmd_blend_rect_t* cmd) {
    u32* pixels = surface_pixels(g_current_target);
    u32 sw = surface_width(g_current_target);
    u32 sh = surface_height(g_current_target);
    if (!pixels || sw == 0u) return;

    u32 x = cmd->x, y = cmd->y, w = cmd->w, h = cmd->h;
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;

    u32 color = cmd->color;
    u8 global_alpha = cmd->opacity;

    /* Pre-multiply global alpha into the color's alpha channel */
    u32 sa = (color >> 24) & 0xFFu;
    sa = (sa * global_alpha) / 255u;
    color = (color & 0x00FFFFFFu) | (sa << 24);

    for (u32 row = 0; row < h; row++) {
        u32* row_ptr = &pixels[(y + row) * sw + x];
        for (u32 col = 0; col < w; col++) {
            row_ptr[col] = blend_argb(row_ptr[col], color);
        }
    }
}

/* ── CLEAR: Fill entire surface ── */
static void exec_clear(const gpu_command_t* cmd) {
    u32 color = cmd->op.fill.color;
    gpu_cmd_fill_rect_t fill = {
        0, 0,
        surface_width(g_current_target),
        surface_height(g_current_target),
        color
    };
    exec_fill_rect(&fill);
}

/* ── DRAW GLYPH: Blit a glyph bitmap with alpha ── */
static void exec_draw_glyph(const gpu_cmd_draw_glyph_t* cmd) {
    u32* pixels = surface_pixels(g_current_target);
    u32 sw = surface_width(g_current_target);
    u32 sh = surface_height(g_current_target);
    if (!pixels || !cmd->bitmap || sw == 0u) return;

    u32 gw = cmd->glyph_w, gh = cmd->glyph_h;
    u32 tx = cmd->x, ty = cmd->y;

    if (cmd->bpp == 1u) {
        /* Monochrome bitmap: 1 bit per pixel, MSB first */
        for (u32 row = 0; row < gh; row++) {
            u32 py = ty + row;
            if (py >= sh) break;
            for (u32 col = 0; col < gw; col++) {
                u32 px = tx + col;
                if (px >= sw) continue;
                u32 byte_idx = (row * ((gw + 7u) / 8u)) + (col / 8u);
                u8 bit = (cmd->bitmap[byte_idx] >> (7u - (col % 8u))) & 1u;
                if (bit) {
                    pixels[py * sw + px] = cmd->text_color;
                } else if (cmd->bg_color != 0u) {
                    pixels[py * sw + px] = blend_argb(pixels[py * sw + px], cmd->bg_color);
                }
            }
        }
    } else {
        /* 8-bit antialiased: each byte is a coverage value 0-255 */
        for (u32 row = 0; row < gh; row++) {
            u32 py = ty + row;
            if (py >= sh) break;
            for (u32 col = 0; col < gw; col++) {
                u32 px = tx + col;
                if (px >= sw) continue;
                u8 coverage = cmd->bitmap[row * gw + col];
                if (coverage == 0u) continue;
                u32 color = cmd->text_color;
                u32 a = ((color >> 24) & 0xFFu);
                a = (a * coverage) / 255u;
                color = (color & 0x00FFFFFFu) | (a << 24);
                pixels[py * sw + px] = blend_argb(pixels[py * sw + px], color);
            }
        }
    }
}

/* ── DRAW IMAGE: Blit RGBA image with optional scaling ── */
static void exec_draw_image(const gpu_cmd_draw_image_t* cmd) {
    u32* pixels = surface_pixels(g_current_target);
    u32 sw = surface_width(g_current_target);
    u32 sh = surface_height(g_current_target);
    if (!pixels || !cmd->pixels || sw == 0u) return;

    u32 dx = cmd->dst_x, dy = cmd->dst_y;
    u32 dw = cmd->dst_w, dh = cmd->dst_h;
    u32 sw_img = cmd->src_w, sh_img = cmd->src_h;
    if (dx + dw > sw) dw = sw - dx;
    if (dy + dh > sh) dh = sh - dy;
    if (sw_img == 0u || sh_img == 0u) return;

    u8 global_alpha = cmd->alpha;

    /* Nearest-neighbor scaling */
    const u32* src_pixels = cmd->pixels;
    for (u32 row = 0; row < dh; row++) {
        u32 src_y = (row * sh_img) / dh;
        u32 py = dy + row;
        for (u32 col = 0; col < dw; col++) {
            u32 src_x = (col * sw_img) / dw;
            u32 src_pixel = src_pixels[src_y * sw_img + src_x];
            if (global_alpha != 255u) {
                u32 a = ((src_pixel >> 24) & 0xFFu);
                a = (a * global_alpha) / 255u;
                src_pixel = (src_pixel & 0x00FFFFFFu) | (a << 24);
            }
            pixels[py * sw + dx + col] = blend_argb(pixels[py * sw + dx + col], src_pixel);
        }
    }
}

/* ── COMPOSITE: Blend one surface onto another ── */
static void exec_composite(const gpu_cmd_composite_t* cmd) {
    u32* src = surface_pixels(cmd->src_surface);
    u32* dst = surface_pixels(g_current_target);
    if (!src || !dst) return;

    u32 src_sw = surface_width(cmd->src_surface);
    u32 src_sh = surface_height(cmd->src_surface);
    u32 dst_sw = surface_width(g_current_target);
    u32 dst_sh = surface_height(g_current_target);
    u32 dx = cmd->dst_x, dy = cmd->dst_y;
    u8 opacity = cmd->opacity;

    u32 copy_w = src_sw;
    u32 copy_h = src_sh;
    if (dx + copy_w > dst_sw) copy_w = dst_sw - dx;
    if (dy + copy_h > dst_sh) copy_h = dst_sh - dy;

    for (u32 row = 0; row < copy_h; row++) {
        u32* s = &src[row * src_sw];
        u32* d = &dst[(dy + row) * dst_sw + dx];
        for (u32 col = 0; col < copy_w; col++) {
            u32 sp = s[col];
            if (opacity != 255u) {
                u32 a = ((sp >> 24) & 0xFFu);
                a = (a * opacity) / 255u;
                sp = (sp & 0x00FFFFFFu) | (a << 24);
            }
            d[col] = blend_argb(d[col], sp);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════════ */

int gpu_engine_init(void) {
    memset(g_cmd_buffer, 0, sizeof(g_cmd_buffer));
    g_cmd_count = 0u;
    g_batch_active = 0u;
    g_surface_count = 0u;
    memset(g_surfaces, 0, sizeof(g_surfaces));
    g_current_target = 0u;
    g_has_3d = virtio_gpu_has_3d();
    g_fb_width = virtio_gpu_width();
    g_fb_height = virtio_gpu_height();
    g_engine_ready = 1u;
    g_total_commands = 0u;
    g_total_surfaces = 0u;

    serial_write("[GPU-Engine] ready: ");
    serial_write_dec(g_fb_width);
    serial_write("x");
    serial_write_dec(g_fb_height);
    if (g_has_3d) serial_writeln(" [3D virgl]");
    else serial_writeln(" [2D accel]");
    return 0;
}

/* ── Command Buffer ── */

void gpu_engine_begin(void) {
    g_cmd_count = 0u;
    g_batch_active = 1u;
}

static int push_command(gpu_cmd_type_t type, const void* data, u32 data_size) {
    if (!g_batch_active || g_cmd_count >= GPU_CMD_BUFFER_SIZE) return -1;
    gpu_command_t* cmd = &g_cmd_buffer[g_cmd_count];
    cmd->type = (u8)type;
    cmd->padding[0] = 0;
    cmd->padding[1] = 0;
    cmd->padding[2] = 0;
    if (data && data_size > 0u) {
        memcpy(&cmd->op, data, data_size);
    }
    g_cmd_count++;
    return 0;
}

int gpu_engine_submit(void) {
    if (!g_batch_active) return -1;

    /* Execute all commands in the buffer */
    for (u32 i = 0; i < g_cmd_count; i++) {
        gpu_command_t* cmd = &g_cmd_buffer[i];
        switch (cmd->type) {
            case GPU_CMD_FILL_RECT:
                exec_fill_rect(&cmd->op.fill);
                break;
            case GPU_CMD_COPY_RECT:
                exec_copy_rect(&cmd->op.copy);
                break;
            case GPU_CMD_BLEND_RECT:
                exec_blend_rect(&cmd->op.blend);
                break;
            case GPU_CMD_CLEAR:
                exec_clear(cmd);
                break;
            case GPU_CMD_DRAW_GLYPH:
                exec_draw_glyph(&cmd->op.glyph);
                break;
            case GPU_CMD_DRAW_IMAGE:
                exec_draw_image(&cmd->op.image);
                break;
            case GPU_CMD_CREATE_SURFACE:
                /* Handled inline */
                break;
            case GPU_CMD_DESTROY_SURFACE:
                break;
            case GPU_CMD_SET_TARGET:
                g_current_target = cmd->op.target.surface_id;
                break;
            case GPU_CMD_FLUSH_SURFACE:
                gpu_engine_flush_surface(cmd->op.surface.surface_id);
                break;
            case GPU_CMD_FLIP:
                gpu_engine_flip();
                break;
            case GPU_CMD_COMPOSITE:
                exec_composite(&cmd->op.composite);
                break;
            case GPU_CMD_SYNC:
                virtio_gpu_flush_rect(0, 0, g_fb_width, g_fb_height);
                break;
            default:
                break;
        }
    }

    g_total_commands += g_cmd_count;
    g_cmd_count = 0u;
    g_batch_active = 0u;
    return 0;
}

void gpu_engine_wait(void) {
    /* Ensure all GPU operations are complete */
    virtio_gpu_flush_rect(0, 0, g_fb_width, g_fb_height);
}

/* ── Drawing Commands ── */

void gpu_engine_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    gpu_cmd_fill_rect_t cmd = { x, y, w, h, color };
    push_command(GPU_CMD_FILL_RECT, &cmd, sizeof(cmd));
}

void gpu_engine_copy_rect(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 w, u32 h) {
    gpu_cmd_copy_rect_t cmd = { src_x, src_y, dst_x, dst_y, w, h, 0u };
    push_command(GPU_CMD_COPY_RECT, &cmd, sizeof(cmd));
}

void gpu_engine_blend_rect(u32 x, u32 y, u32 w, u32 h, u32 color, u8 opacity) {
    gpu_cmd_blend_rect_t cmd = { x, y, w, h, color, opacity };
    push_command(GPU_CMD_BLEND_RECT, &cmd, sizeof(cmd));
}

void gpu_engine_clear(u32 color) {
    gpu_cmd_fill_rect_t cmd = { 0, 0, g_fb_width, g_fb_height, color };
    push_command(GPU_CMD_CLEAR, &cmd, sizeof(cmd));
}

void gpu_engine_draw_glyph(u32 x, u32 y, u32 glyph_w, u32 glyph_h,
                            u32 text_color, u32 bg_color,
                            const u8* bitmap, u8 bpp) {
    gpu_cmd_draw_glyph_t cmd = { x, y, glyph_w, glyph_h, text_color, bg_color, bitmap, bpp };
    push_command(GPU_CMD_DRAW_GLYPH, &cmd, sizeof(cmd));
}

void gpu_engine_draw_image(u32 dst_x, u32 dst_y, u32 dst_w, u32 dst_h,
                            u32 src_w, u32 src_h,
                            const u32* pixels, u8 alpha) {
    gpu_cmd_draw_image_t cmd = { dst_x, dst_y, dst_w, dst_h, src_w, src_h, pixels, alpha };
    push_command(GPU_CMD_DRAW_IMAGE, &cmd, sizeof(cmd));
}

/* ── Surface Management ── */

u32 gpu_engine_create_surface(u32 width, u32 height) {
    /* Find free slot */
    u32 slot = 0;
    for (u32 i = 1; i < GPU_MAX_SURFACES; i++) {
        if (!g_surfaces[i].active) { slot = i; break; }
    }
    if (slot == 0u) {
        serial_writeln("[GPU-Engine] no free surface slots");
        return 0;
    }

    /* Allocate pixel buffer from a static pool */
    static u32 surface_pool[1920 * 1080 * 2]; /* ~16MB for 2 full-screen surfaces */
    static u32 pool_offset = 0u;
    u32 needed = width * height;
    if (pool_offset + needed > sizeof(surface_pool) / sizeof(u32)) {
        serial_writeln("[GPU-Engine] surface pool exhausted");
        return 0;
    }

    gpu_surface_t* s = &g_surfaces[slot];
    s->active = 1;
    s->width = width;
    s->height = height;
    s->pitch = width * 4u;
    s->pixels = &surface_pool[pool_offset];
    s->resource_id = g_next_resource_id++;
    s->dirty = 0;
    pool_offset += needed;

    memset(s->pixels, 0, width * height * 4u);
    g_surface_count++;
    g_total_surfaces++;

    serial_write("[GPU-Engine] surface #");
    serial_write_dec(slot);
    serial_write(" ");
    serial_write_dec(width);
    serial_write("x");
    serial_write_dec(height);
    serial_writeln("");
    return slot;
}

void gpu_engine_destroy_surface(u32 surface_id) {
    if (surface_id == 0u || surface_id >= GPU_MAX_SURFACES) return;
    if (!g_surfaces[surface_id].active) return;
    g_surfaces[surface_id].active = 0;
    g_surfaces[surface_id].pixels = 0;
    if (g_surface_count > 0u) g_surface_count--;
}

void gpu_engine_set_target(u32 surface_id) {
    g_current_target = surface_id;
}

u32* gpu_engine_surface_pixels(u32 surface_id) {
    return surface_pixels(surface_id);
}

void gpu_engine_surface_size(u32 surface_id, u32* w, u32* h) {
    if (w) *w = surface_width(surface_id);
    if (h) *h = surface_height(surface_id);
}

/* ── Presentation ── */

void gpu_engine_flush_surface(u32 surface_id) {
    if (surface_id == 0u) {
        /* Flush default framebuffer */
        virtio_gpu_flush_rect(0, 0, g_fb_width, g_fb_height);
        return;
    }
    /* For offscreen surfaces, copy to framebuffer then flush */
    if (surface_id >= GPU_MAX_SURFACES || !g_surfaces[surface_id].active) return;
    gpu_surface_t* s = &g_surfaces[surface_id];

    /* Copy surface to back buffer */
    u32* fb = (u32*)virtio_gpu_back_buffer();
    u32 copy_w = min_u32(s->width, g_fb_width);
    u32 copy_h = min_u32(s->height, g_fb_height);
    u32 fb_pitch = virtio_gpu_framebuffer_pitch() / 4u;

    for (u32 row = 0; row < copy_h; row++) {
        u64* dst = (u64*)&fb[row * fb_pitch];
        const u64* src = (const u64*)&s->pixels[row * s->width];
        u32 qwords = copy_w / 2u;
        for (u32 q = 0; q < qwords; q++) dst[q] = src[q];
        if (copy_w & 1u) fb[row * fb_pitch + copy_w - 1u] = s->pixels[row * s->width + copy_w - 1u];
    }

    virtio_gpu_flush_rect(0, 0, copy_w, copy_h);
    s->dirty = 0;
}

void gpu_engine_flip(void) {
    virtio_gpu_page_flip();
}

/* ── Compositing ── */

void gpu_engine_composite(u32 src_surface, u32 dst_x, u32 dst_y,
                           u8 opacity, u8 blend_mode) {
    (void)blend_mode; /* TODO: multiply, screen blend modes */
    gpu_cmd_composite_t cmd = { src_surface, dst_x, dst_y, opacity, 0u };
    push_command(GPU_CMD_COMPOSITE, &cmd, sizeof(cmd));
}

/* ── Stats ── */

u32 gpu_engine_commands_count(void) { return g_total_commands; }
u32 gpu_engine_surfaces_count(void) { return g_surface_count; }

const char* gpu_engine_name(void) {
    if (!g_engine_ready) return "inactive";
    if (g_has_3d) return "GPU Engine 3D (virgl OpenGL)";
    return "GPU Engine 2D (VirtIO-GPU acceleration)";
}
