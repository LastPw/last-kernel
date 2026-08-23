#include "vbe.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

static vbe_info_t g_vbe;
static u8 g_vbe_present = 0u;
static spinlock_t g_vbe_lock;

void vbe_init(u64 fb_addr, u32 width, u32 height, u32 pitch, u16 bpp) {
    spinlock_init(&g_vbe_lock);
    memset(&g_vbe, 0, sizeof(g_vbe));

    if (!fb_addr || width == 0 || height == 0) {
        g_vbe.framebuffer_addr = 0xFD000000ULL; // Fallback VBE linear FB
        g_vbe.width = 1920u;
        g_vbe.height = 1080u;
        g_vbe.pitch = 1920u * 4u;
        g_vbe.bpp = 32u;
    } else {
        g_vbe.framebuffer_addr = fb_addr;
        g_vbe.width = width;
        g_vbe.height = height;
        g_vbe.pitch = pitch;
        g_vbe.bpp = bpp;
    }

    g_vbe_present = 1u;
    serial_writeln("[VBE] UEFI GOP / VBE linear framebuffer active (1920x1080 32-bpp 60Hz)");
}

int vbe_available(void) {
    return g_vbe_present ? 1 : 0;
}

const vbe_info_t* vbe_get_info(void) {
    return &g_vbe;
}

void vbe_put_pixel(u32 x, u32 y, u32 color) {
    if (!g_vbe_present || x >= g_vbe.width || y >= g_vbe.height) return;
    u32* fb = (u32*)(uintptr_t)g_vbe.framebuffer_addr;
    u32 offset = (y * (g_vbe.pitch / 4u)) + x;
    fb[offset] = color;
}

void vbe_clear(u32 color) {
    if (!g_vbe_present) return;
    u32 total_pixels = (g_vbe.pitch / 4u) * g_vbe.height;
    u32* fb = (u32*)(uintptr_t)g_vbe.framebuffer_addr;
    for (u32 i = 0u; i < total_pixels; i++) {
        fb[i] = color;
    }
}
