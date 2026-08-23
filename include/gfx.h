#ifndef GFX_H
#define GFX_H

#include "types.h"

/* Record the framebuffer Limine configured at boot (already HHDM-mapped). */
void gfx_set_boot_framebuffer(u64 phys, u64 virt, u32 width, u32 height, u32 pitch, u16 bpp);
int gfx_enter_graphics(void);
int gfx_set_mode(u16 width, u16 height, u16 bpp);
void gfx_exit_graphics(void);
void gfx_restore_text(void);

void gfx_clear(u32 color);
void gfx_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void gfx_round_rect(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 color);
void gfx_round_rect_aa(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 border_color, u32 fill_color, u8 has_border);
void gfx_circle_aa(u32 cx, u32 cy, u32 radius, u32 color, u8 alpha);

/* Copy an XRGB8888 rectangle in physical display coordinates. */
void gfx_blit(u32 x, u32 y, u32 w, u32 h, const u32* pixels);
void gfx_read_rect(u32 x, u32 y, u32 w, u32 h, u32* out_pixels);
void gfx_text(u32 x, u32 y, u32 color, const char* text);
void gfx_px_blend(u32 x, u32 y, u32 color, u8 alpha);

/* Damage Tracking & Hardware Blitting (144Hz+ Esports Engine) */
void gfx_damage_reset(void);
void gfx_damage_add(u32 x, u32 y, u32 w, u32 h);
void gfx_damage_add_full(void);
void gfx_present_damaged(void);

/* ── 64-bit Deep Color & HDR Graphics Engine ── */
typedef struct {
    u16 r; /* 16-bit Red Channel: 0..65535 */
    u16 g; /* 16-bit Green Channel: 0..65535 */
    u16 b; /* 16-bit Blue Channel: 0..65535 */
    u16 a; /* 16-bit Alpha Channel: 0..65535 */
} rgba64_t;

void gfx_enable_hdr64(u8 enable);
u8 gfx_is_hdr64(void);
void gfx_px_blend64(u32 x, u32 y, u16 r, u16 g, u16 b, u16 a);
void gfx_rect64(u32 x, u32 y, u32 w, u32 h, rgba64_t color);
void gfx_gradient_v64(u32 x, u32 y, u32 w, u32 h, rgba64_t top, rgba64_t bottom);
void gfx_gradient_h64(u32 x, u32 y, u32 w, u32 h, rgba64_t left, rgba64_t right);
rgba64_t gfx_rgb_to_rgba64(u32 color, u8 alpha);
u32 gfx_rgba64_to_rgb(rgba64_t c);

/* ── True Frosted Glass Gaussian Box Blur Engine ── */
void gfx_frosted_glass_blur(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 tint_color, u8 tint_alpha);

/* ── Hardware Cursor Plane with Alpha Shadow ── */
void gfx_draw_cursor(u32 mx, u32 my);

/* ── Tile-Based Damage Tracking (Tiled Compositor) ── */
void gfx_tile_damage_add(u32 x, u32 y, u32 w, u32 h);
void gfx_tile_present(void);

/* Atomically blit the backbuffer to VRAM for 0-flicker double-buffered rendering. */
void gfx_present(void);
void gfx_present_rect(u32 x, u32 y, u32 w, u32 h);
u32 gfx_width(void);
u32 gfx_height(void);
u32 gfx_bpp(void);
const char* gfx_backend_name(void);

#endif
