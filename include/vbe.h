#ifndef VBE_H
#define VBE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u64 framebuffer_addr;
    u32 width;
    u32 height;
    u32 pitch;
    u16 bpp;
    u8  red_mask_shift;
    u8  green_mask_shift;
    u8  blue_mask_shift;
    u32 clip_x;
    u32 clip_y;
    u32 clip_w;
    u32 clip_h;
} vbe_info_t;

/* Public API */
void vbe_init(u64 fb_addr, u32 width, u32 height, u32 pitch, u16 bpp);
int  vbe_available(void);
const vbe_info_t* vbe_get_info(void);
void vbe_put_pixel(u32 x, u32 y, u32 color);
void vbe_clear(u32 color);
void vbe_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void vbe_draw_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void vbe_draw_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 color);
void vbe_draw_circle(i32 xc, i32 yc, i32 r, u32 color);
void vbe_fill_circle(i32 xc, i32 yc, i32 r, u32 color);
void vbe_blend_pixel(u32 x, u32 y, u32 color, u8 alpha);
void vbe_blit(const u32* src_buf, u32 src_pitch_pixels, u32 dst_x, u32 dst_y, u32 w, u32 h);
void vbe_set_clip_rect(u32 x, u32 y, u32 w, u32 h);
void vbe_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* VBE_H */
