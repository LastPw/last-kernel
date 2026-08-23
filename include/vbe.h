#ifndef VBE_H
#define VBE_H

#include "types.h"

typedef struct {
    u64 framebuffer_addr;
    u32 width;
    u32 height;
    u32 pitch;
    u16 bpp;
    u8  red_mask_shift;
    u8  green_mask_shift;
    u8  blue_mask_shift;
} vbe_info_t;

void vbe_init(u64 fb_addr, u32 width, u32 height, u32 pitch, u16 bpp);
int vbe_available(void);
const vbe_info_t* vbe_get_info(void);
void vbe_put_pixel(u32 x, u32 y, u32 color);
void vbe_clear(u32 color);

#endif /* VBE_H */
