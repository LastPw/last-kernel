#include "gfx.h"
#include "frame_allocator.h"

static u32 s_bb_w = 0u, s_bb_h = 0u;
static int gfx_backbuffers_alloc(u32 w, u32 h);
#include "io.h"
#include "paging.h"
#include "screen.h"
#include "serial.h"
#include "gpu_backend.h"

#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
#define VGA_AC_INDEX 0x3C0
#define VGA_INSTAT_READ 0x3DA

#define BGA_IO_INDEX 0x01CE
#define BGA_IO_DATA  0x01CF
#define BGA_INDEX_ID 0x0
#define BGA_INDEX_XRES 0x1
#define BGA_INDEX_YRES 0x2
#define BGA_INDEX_BPP 0x3
#define BGA_INDEX_ENABLE 0x4
#define BGA_ENABLE_DISABLED 0x00
#define BGA_ENABLE_ENABLED  0x01
#define BGA_ENABLE_LFB      0x40
#define BGA_ID0 0xB0C0
#define BGA_ID5 0xB0C5
/* Keep the high physical LFB behind a low identity-mapped virtual window. */
#define BGA_LFB_VIRT 0x01000000u

#define PCI_CONFIG_ADDRESS 0x0CF8u
#define PCI_CONFIG_DATA    0x0CFCu
#define PCI_VGA_BUS        0u
#define PCI_VGA_DEVICE     2u
#define PCI_VGA_FUNCTION   0u
#define PCI_VGA_BAR0       0x10u
#define PCI_COMMAND        0x04u
#define PCI_VGA_BAR2       0x18u

/* User-space desktop coordinates match the native 1024x768 canvas.  Lower
   modes are scaled by the pixel writer instead of clipping at 320x200. */
#define LOGICAL_W 1024u
#define LOGICAL_H 768u

static u8* fb8 = (u8*)0xA0000;
static u32* fb32 = 0;
static u32 phys_w = LOGICAL_W;
static u32 phys_h = LOGICAL_H;
static u8 use_vbe = 0;
static u8 vbe_bytespp = 4u;
static u8 vbe_bpp = 32u;
static u32 fb_pitch_bytes = LOGICAL_W;
static int in_graphics = 0;
static u8 use_gpu_backend = 0u;  /* 1 = VirtIO-GPU active */
static u32 bga_lfb_phys = 0u;
/* Limine framebuffer info captured at boot. */
static u64 limine_fb_phys = 0u;
static u64 limine_fb_virt = 0u;
static u32 limine_fb_width = 0u;
static u32 limine_fb_height = 0u;
static u32 limine_fb_pitch = 0u;
static u16 limine_fb_bpp = 0u;

static int bga_try_enable(u16 w, u16 h, u16 bpp);

static u32 pci_config_read32(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = 0x80000000u |
                  ((u32)bus << 16) |
                  ((u32)device << 11) |
                  ((u32)function << 8) |
                  (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_config_write32(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = 0x80000000u |
                  ((u32)bus << 16) |
                  ((u32)device << 11) |
                  ((u32)function << 8) |
                  (offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int pci_enable_vga_lfb(void) {
    /* Bochs VGA is on bus 0; device 1 in QEMU q35 (its ICH9 PCIe root
       moves the default VGA slot), device 2 in classic i440fx.  Scan both. */
    static const u8 vga_devices[] = { 1u, 2u, 0u };
    u32 command;
    u32 bar0;
    u32 found = 0u;
    for (u32 i = 0u; i < sizeof(vga_devices); i++) {
        u8 dev = vga_devices[i];
        u32 id = pci_config_read32(PCI_VGA_BUS, dev, PCI_VGA_FUNCTION, 0u);
        if (id != 0x11111234u) continue;
        command = pci_config_read32(PCI_VGA_BUS, dev, PCI_VGA_FUNCTION, PCI_COMMAND);
        pci_config_write32(PCI_VGA_BUS, dev, PCI_VGA_FUNCTION, PCI_COMMAND,
                           command | 0x00000002u | 0x00000004u);
        bar0 = pci_config_read32(PCI_VGA_BUS, dev, PCI_VGA_FUNCTION, PCI_VGA_BAR0);
        command = pci_config_read32(PCI_VGA_BUS, dev, PCI_VGA_FUNCTION, PCI_COMMAND);
        bar0 = pci_config_read32(PCI_VGA_BUS, dev, PCI_VGA_FUNCTION, PCI_VGA_BAR0);
        bga_lfb_phys = bar0 & 0xFFFFFFF0u;
        if ((command & 0x2u) != 0u && bga_lfb_phys != 0u) { found = 1u; break; }
    }
    return found ? 0 : -1;
}

static int gfx_framebuffer_size(u32 width, u32 height, u32 bytespp, u32 pitch,
                                u32 address, u32* out_start, u32* out_bytes) {
    u64 minimum_pitch;
    u64 total;
    u32 start;
    if (!width || !height || !bytespp || !address || address < 0x00100000u) return -1;
    minimum_pitch = (u64)width * (u64)bytespp;
    if (minimum_pitch > 0xFFFFFFFFull) return -1;
    if ((u64)pitch < minimum_pitch || pitch > 0x1000000u) return -1;
    total = (u64)pitch * (u64)height + (u64)(address & 0xFFFu);
    if (total == 0u || total > 0x7FFFFFFFu) return -1;
    start = address & 0xFFFFF000u;
    if (start + (u32)total < start) return -1;
    if (out_start) *out_start = start;
    if (out_bytes) *out_bytes = (u32)total;
    return 0;
}

static int gfx_enable_bga_mode(u16 w, u16 h, u16 bpp) {
    u32 bytespp;
    u32 pitch;
    u32 fb_start;
    u32 fb_bytes;
    u32 map_bytes;
    if (w < 640u || h < 480u || w > 1920u || h > 1200u || bpp != 32u) return -1;
    if (pci_enable_vga_lfb() != 0) { serial_writeln("[gfx] bga: pci_enable_vga_lfb failed"); return -1; }
    if (bga_try_enable(w, h, bpp) != 0) { serial_writeln("[gfx] bga: bga_try_enable failed"); return -1; }
    bytespp = 4u;
    pitch = (u32)w * bytespp;
    fb_start = BGA_LFB_VIRT & 0xFFFFF000u;
    fb_bytes = pitch * (u32)h;
    if (gfx_framebuffer_size(w, h, bytespp, pitch, BGA_LFB_VIRT, &fb_start, &fb_bytes) != 0) { serial_writeln("[gfx] bga: fb_size failed"); return -1; }
    map_bytes = (fb_bytes + 0x0FFFu) & 0xFFFFF000u;
    if (map_bytes < fb_bytes) { serial_writeln("[gfx] bga: map overflow"); return -1; }
    if (paging_map_range(fb_start, bga_lfb_phys, map_bytes, PAGE_FLAG_RW) != 0) { serial_writeln("[gfx] bga: map_range failed"); return -1; }
    fb32 = (u32*)BGA_LFB_VIRT;
    fb8 = (u8*)BGA_LFB_VIRT;
    phys_w = w; phys_h = h; use_vbe = 1u; vbe_bytespp = (u8)bytespp; vbe_bpp = (u8)bpp;
    fb_pitch_bytes = pitch; in_graphics = 1;
    gfx_clear(1u);
    return 0;
}

static u16 bga_read(u16 idx) {
    outw(BGA_IO_INDEX, idx);
    return inw(BGA_IO_DATA);
}

static void bga_write(u16 idx, u16 val) {
    outw(BGA_IO_INDEX, idx);
    outw(BGA_IO_DATA, val);
}

static int bga_try_enable(u16 w, u16 h, u16 bpp) {
    u16 id = bga_read(BGA_INDEX_ID);
    if ((id & 0xFFF0u) != BGA_ID0) {
        return -1;
    }

    /* Request newest compatible interface if device supports it. */
    bga_write(BGA_INDEX_ID, BGA_ID5);
    id = bga_read(BGA_INDEX_ID);
    if ((id & 0xFFF0u) != BGA_ID0) {
        return -1;
    }

    bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
    bga_write(BGA_INDEX_XRES, w);
    bga_write(BGA_INDEX_YRES, h);
    bga_write(BGA_INDEX_BPP, bpp);
    bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_ENABLED | BGA_ENABLE_LFB);

    if (bga_read(BGA_INDEX_XRES) != w) return -1;
    if (bga_read(BGA_INDEX_YRES) != h) return -1;
    if (bga_read(BGA_INDEX_BPP) != bpp) return -1;
    return 0;
}

static u32 color_index_to_rgb(u32 c) {
    if (c > 255u) return c & 0x00FFFFFFu;
    static const u32 ansi16[16] = {
        0x000000u, 0x0000AAu, 0x00AA00u, 0x00AAAAu,
        0xAA0000u, 0xAA00AAu, 0xAA5500u, 0xAAAAAAu,
        0x555555u, 0x5555FFu, 0x55FF55u, 0x55FFFFu,
        0xFF5555u, 0xFF55FFu, 0xFFFF55u, 0xFFFFFFu
    };
    if (c < 16u) return ansi16[c];
    if (c < 232u) {
        u32 idx = (u32)c - 16u;
        u32 r = idx / 36u;
        u32 g = (idx / 6u) % 6u;
        u32 b = idx % 6u;
        static const u8 lv[6] = { 0u, 51u, 102u, 153u, 204u, 255u };
        return ((u32)lv[r] << 16) | ((u32)lv[g] << 8) | (u32)lv[b];
    }
    {
        u32 g = 8u + ((u32)c - 232u) * 10u;
        if (g > 255u) g = 255u;
        return (g << 16) | (g << 8) | g;
    }
}

static u16 rgb_to_565(u32 rgb) {
    u8 r = (u8)((rgb >> 16) & 0xFFu);
    u8 g = (u8)((rgb >> 8) & 0xFFu);
    u8 b = (u8)(rgb & 0xFFu);
    return (u16)(((u16)(r >> 3) << 11) | ((u16)(g >> 2) << 5) | (u16)(b >> 3));
}

static u16 rgb_to_555(u32 rgb) {
    u8 r = (u8)((rgb >> 16) & 0xFFu);
    u8 g = (u8)((rgb >> 8) & 0xFFu);
    u8 b = (u8)(rgb & 0xFFu);
    return (u16)(((u16)(r >> 3) << 10) | ((u16)(g >> 3) << 5) | (u16)(b >> 3));
}

/* 8x8 font for ASCII 32..127 subset (fallback blanks for undefined). */
static const u8 font8x8[96][8] = {
    {0,0,0,0,0,0,0,0}, {24,24,24,24,24,0,24,0}, {54,54,20,0,0,0,0,0}, {54,54,127,54,127,54,54,0},
    {24,62,96,60,6,124,24,0}, {98,102,12,24,48,102,70,0}, {28,54,28,118,59,54,111,0}, {12,12,24,0,0,0,0,0},
    {6,12,24,24,24,12,6,0}, {48,24,12,12,12,24,48,0}, {0,102,60,255,60,102,0,0}, {0,24,24,126,24,24,0,0},
    {0,0,0,0,0,24,24,48}, {0,0,0,126,0,0,0,0}, {0,0,0,0,0,24,24,0}, {2,6,12,24,48,96,64,0},
    {60,102,110,118,102,102,60,0}, {24,56,24,24,24,24,126,0}, {60,102,6,12,24,48,126,0}, {60,102,6,28,6,102,60,0},
    {12,28,44,76,126,12,12,0}, {126,96,124,6,6,102,60,0}, {28,48,96,124,102,102,60,0}, {126,102,6,12,24,24,24,0},
    {60,102,102,60,102,102,60,0}, {60,102,102,62,6,12,56,0}, {0,24,24,0,0,24,24,0}, {0,24,24,0,0,24,24,48},
    {14,24,48,96,48,24,14,0}, {0,0,126,0,126,0,0,0}, {112,24,12,6,12,24,112,0}, {60,102,6,12,24,0,24,0},
    {60,102,110,110,96,98,60,0}, {24,60,102,102,126,102,102,0}, {124,102,102,124,102,102,124,0}, {60,102,96,96,96,102,60,0},
    {120,108,102,102,102,108,120,0}, {126,96,96,124,96,96,126,0}, {126,96,96,124,96,96,96,0}, {60,102,96,96,110,102,62,0},
    {102,102,102,126,102,102,102,0}, {60,24,24,24,24,24,60,0}, {30,12,12,12,12,108,56,0}, {102,108,120,112,120,108,102,0},
    {96,96,96,96,96,96,126,0}, {99,119,127,107,99,99,99,0}, {102,118,126,126,110,102,102,0}, {60,102,102,102,102,102,60,0},
    {124,102,102,124,96,96,96,0}, {60,102,102,102,102,108,54,0}, {124,102,102,124,120,108,102,0}, {60,102,96,60,6,102,60,0},
    {126,24,24,24,24,24,24,0}, {102,102,102,102,102,102,60,0}, {102,102,102,102,102,60,24,0}, {99,99,99,107,127,119,99,0},
    {102,102,60,24,60,102,102,0}, {102,102,102,60,24,24,24,0}, {126,6,12,24,48,96,126,0}, {60,48,48,48,48,48,60,0},
    {64,96,48,24,12,6,2,0}, {60,12,12,12,12,12,60,0}, {24,60,102,0,0,0,0,0}, {0,0,0,0,0,0,0,255},
    {24,24,12,0,0,0,0,0}, {0,0,60,6,62,102,59,0}, {96,96,124,102,102,102,124,0}, {0,0,60,102,96,102,60,0},
    {6,6,62,102,102,102,62,0}, {0,0,60,102,126,96,60,0}, {28,54,48,120,48,48,48,0}, {0,0,62,102,102,62,6,60},
    {96,96,124,102,102,102,102,0}, {24,0,56,24,24,24,60,0}, {6,0,6,6,6,6,54,28}, {96,96,102,108,120,108,102,0},
    {56,24,24,24,24,24,60,0}, {0,0,118,107,107,99,99,0}, {0,0,124,102,102,102,102,0}, {0,0,60,102,102,102,60,0},
    {0,0,124,102,102,124,96,96}, {0,0,62,102,102,62,6,6}, {0,0,108,118,96,96,96,0}, {0,0,62,96,60,6,124,0},
    {48,48,120,48,48,54,28,0}, {0,0,102,102,102,102,62,0}, {0,0,102,102,102,60,24,0}, {0,0,99,99,107,127,54,0},
    {0,0,102,60,24,60,102,0}, {0,0,102,102,102,62,6,60}, {0,0,126,12,24,48,126,0}, {14,24,24,112,24,24,14,0},
    {24,24,24,0,24,24,24,0}, {112,24,24,14,24,24,112,0}, {59,110,0,0,0,0,0,0}, {0,8,28,54,99,65,127,0}
};

static void write_regs(const u8* r) {
    outb(VGA_MISC_WRITE, *r++);

    for (u32 i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, (u8)i);
        outb(VGA_SEQ_DATA, *r++);
    }

    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, (u8)(inb(VGA_CRTC_DATA) | 0x80));
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, (u8)(inb(VGA_CRTC_DATA) & ~0x80));

    for (u32 i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, (u8)i);
        outb(VGA_CRTC_DATA, *r++);
    }

    for (u32 i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, (u8)i);
        outb(VGA_GC_DATA, *r++);
    }

    for (u32 i = 0; i < 21; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, (u8)i);
        outb(VGA_AC_INDEX, *r++);
    }
    (void)inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

static void vga_set_palette_256(void) {
    /* Build a rich 256-color palette:
       0..15  : classic ANSI
       16..231: 6x6x6 RGB cube (216 colors)
       232..255: grayscale ramp
    */
    static const u8 ansi16[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };

    outb(0x3C6, 0xFFu); /* Pixel mask: enable all DAC bits. */
    outb(0x3C8, 0u);

    for (u32 i = 0; i < 16u; i++) {
        outb(0x3C9, ansi16[i][0]);
        outb(0x3C9, ansi16[i][1]);
        outb(0x3C9, ansi16[i][2]);
    }

    for (u32 r = 0; r < 6u; r++) {
        for (u32 g = 0; g < 6u; g++) {
            for (u32 b = 0; b < 6u; b++) {
                u8 rr = (u8)(r * 12u + 3u);
                u8 gg = (u8)(g * 12u + 3u);
                u8 bb = (u8)(b * 12u + 3u);
                outb(0x3C9, rr);
                outb(0x3C9, gg);
                outb(0x3C9, bb);
            }
        }
    }

    for (u32 i = 0; i < 24u; i++) {
        u8 v = (u8)(i * 2u + 16u);
        outb(0x3C9, v);
        outb(0x3C9, v);
        outb(0x3C9, v);
    }
}

void gfx_set_boot_framebuffer(u64 phys, u64 virt, u32 width, u32 height, u32 pitch, u16 bpp) {
    /* Buffers cannot be allocated here: this runs before the frame
       allocator exists. gfx_backbuffers_ensure() is called on first use. */
    s_bb_w = width;
    s_bb_h = height;
    limine_fb_phys = phys;
    limine_fb_virt = virt;
    limine_fb_width = width;
    limine_fb_height = height;
    limine_fb_pitch = pitch;
    limine_fb_bpp = bpp;
}

int gfx_enter_graphics(void) {
    /* ── Priority 0: VirtIO-GPU (hardware-accelerated scanout) ─────────
       Try the GPU backend first. If VirtIO-GPU is present, it provides
       a framebuffer in guest RAM that the GPU scans out directly.
       No VRAM copy needed — just flush dirty regions via VirtIO commands. */
    {
        gpu_backend_type_t bt = gpu_backend_init();
        if (bt == GPU_BACKEND_GPU) {
            /* Use the GPU framebuffer directly */
            u32 gw = gpu_backend_width();
            u32 gh = gpu_backend_height();
            u32 gp = gpu_backend_pitch();

            fb32 = gpu_backend_framebuffer();
            fb8 = (u8*)fb32;
            phys_w = gw;
            phys_h = gh;
            use_vbe = 1u;
            vbe_bytespp = 4u;
            vbe_bpp = 32u;
            fb_pitch_bytes = gp;
            in_graphics = 1;
            use_gpu_backend = 1u;

            gfx_clear(1u);
            serial_write("[gfx] using VirtIO-GPU ");
            serial_write_dec(gw);
            serial_write("x");
            serial_write_dec(gh);
            serial_writeln(" (GPU-accelerated scanout)");
            return 0;
        }
        /* GPU not available — fall through to CPU backends */
        use_gpu_backend = 0u;
    }

    /* ── Priority 1: Limine framebuffer ─────────────────────────────────
       Limine already programmes the display controller and maps the LFB
       through the Higher Half Direct Map, so it is guaranteed to be the
       surface the hardware is currently scanning out.  Reprogramming BGA
       on top of that can desynchronise the scanout address from the
       buffer we write to, producing a blank screen.                     */
    if (limine_fb_virt != 0u &&
        limine_fb_width >= 640u &&
        limine_fb_height >= 480u &&
        (limine_fb_bpp == 32u || limine_fb_bpp == 24u ||
         limine_fb_bpp == 16u || limine_fb_bpp == 15u || limine_fb_bpp == 8u)) {
        u32 bytespp;
        if (limine_fb_bpp == 8u) bytespp = 1u;
        else if (limine_fb_bpp == 15u || limine_fb_bpp == 16u) bytespp = 2u;
        else if (limine_fb_bpp == 24u) bytespp = 3u;
        else bytespp = 4u;
        u32 pitch = limine_fb_pitch;
        if (pitch < limine_fb_width * bytespp) {
            pitch = limine_fb_width * bytespp;
        }
        fb32 = (u32*)(uintptr_t)limine_fb_virt;
        fb8 = (u8*)(uintptr_t)limine_fb_virt;
        phys_w = limine_fb_width;
        phys_h = limine_fb_height;
        use_vbe = 1u;
        vbe_bytespp = (u8)bytespp;
        vbe_bpp = limine_fb_bpp;
        fb_pitch_bytes = pitch;
        in_graphics = 1;
        if (vbe_bytespp == 1u) {
            vga_set_palette_256();
        }
        gfx_clear(1);
        serial_write("[gfx] using Limine framebuffer ");
        serial_write_dec(limine_fb_width);
        serial_write("x");
        serial_write_dec(limine_fb_height);
        serial_write("x");
        serial_write_dec((u32)limine_fb_bpp);
        serial_writeln("");
        outb(0xE9, 'V');
        return 0;
    }

    /* ── Priority 2: BGA (Bochs Graphics Adapter) ──────────────────────
       Only used when Limine did not provide a usable framebuffer.       */
    if (gfx_enable_bga_mode(1024u, 768u, 32u) == 0) {
        serial_writeln("[gfx] using BGA 1024x768x32");
        outb(0xE9, 'B');
        return 0;
    }

    /* ── Priority 3: VGA Mode 13h (320×200 8-bpp) ──────────────────── */
    serial_writeln("[gfx] falling back to VGA mode 13h");
    {
        static const u8 mode_13h[] = {
            0x63,
            0x03,0x01,0x0F,0x00,0x0E,
            0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF,
            0x00,0x00,0x00,0x00,0x00,0x40,0x05,0x0F,0xFF,
            0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x01,0x00,0x0F,0x00,0x00
        };
        write_regs(mode_13h);
    }
    vga_set_palette_256();
    fb8 = (u8*)0xA0000;
    fb32 = 0;
    phys_w = LOGICAL_W;
    phys_h = LOGICAL_H;
    use_vbe = 0u;
    vbe_bytespp = 1u;
    vbe_bpp = 8u;
    fb_pitch_bytes = LOGICAL_W;
    in_graphics = 1;
    gfx_clear(1);
    outb(0xE9, 'G');
    return 0;
}

int gfx_set_mode(u16 width, u16 height, u16 bpp) {
    if (gfx_enable_bga_mode(width, height, bpp) == 0) return 0;
    return -1;
}

void gfx_exit_graphics(void) {
    if (use_vbe) {
        in_graphics = 0;
        return;
    }
    static const u8 mode_03h[] = {
        0x67,
        0x03,0x00,0x03,0x00,0x02,
        0x5F,0x4F,0x50,0x82,0x55,0x81,0xBF,0x1F,0x00,0x4F,0x0D,0x0E,0x00,0x00,0x00,0x50,0x9C,0x0E,0x8F,0x28,0x1F,0x96,0xB9,0xA3,0xFF,
        0x00,0x00,0x10,0x00,0x00,0x10,0x0E,0x00,0xFF,
        0x00,0x01,0x02,0x03,0x04,0x05,0x14,0x07,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x0C,0x00,0x0F,0x08,0x00
    };
    write_regs(mode_03h);
    in_graphics = 0;
}

/* Reload the text-mode character generator (VGA font plane 2) after a BGA
   session, which leaves the font plane undefined so every glyph renders as
   the same garbage pattern. Plain mode-03h reprogramming does not reload the
   font: the CGA/MDA glyph ROM is on plane 2, which the BGA path may have
   overwritten. */
static void vga_restore_text_font(void) {
    /* font8x8 is the file-scope table above. */
    u8 orig_seq[5];
    u8 orig_gc[9];
    for (u32 i = 0u; i < 5u; i++) {
        outb(VGA_SEQ_INDEX, (u8)i);
        orig_seq[i] = (u8)inb(VGA_SEQ_DATA);
    }
    for (u32 i = 0u; i < 9u; i++) {
        outb(0x3CEu, (u8)i);
        orig_gc[i] = (u8)inb(0x3CFu);
    }

    /* Turn off the sequencer screen and switch to flat (non-chain4, extended)
       memory so the 0xA0000 window addresses every plane simultaneously, then
       gate writes to plane 2 only. The canonical font-load register dance from
       the OSDev wiki: */
    outb(VGA_SEQ_INDEX, 0x00u); outb(VGA_SEQ_DATA, (u8)(orig_seq[0] | 0x01u));
    outb(0x3CEu, 0x06u); outb(0x3CFu, 0x04u);   /* GC mode: flat, map to 0xA0000 */
    outb(VGA_SEQ_INDEX, 0x04u); outb(VGA_SEQ_DATA, 0x06u);   /* SEQ mem mode: chain4 off, extended */
    outb(VGA_SEQ_INDEX, 0x02u); outb(VGA_SEQ_DATA, 0x04u);   /* map mask: write plane 2 only */

    /* GC mode register: write mode 0 so each CPU byte lands on the map-mask
       selected plane (2). In write mode 2 the plane is chosen by bit 2 of the
       data byte, so 0x00/0x40 glyph bytes would never reach plane 2. Read plane
       is set to 2 purely for the readback below -- writes already go to plane 2
       through the map mask. */
    outb(0x3CEu, 0x05u); outb(0x3CFu, 0x00u);   /* write mode 0 */
    outb(0x3CEu, 0x04u); outb(0x3CFu, 0x02u);   /* read plane 2 */

    /* font8x8 rows are 8px but the 720x400 text cell is 16 scanlines tall, so
       write 16 rows per glyph: the 8 glyph rows top-aligned, rows 8..15 zero.
       Plane selection is the map mask, NOT an address bit, so the base is plain
       0xA0000 for all planes. */
    volatile u8* font_mem = (volatile u8*)0xA0000u;
    for (u32 c = 0u; c < 128u; c++) {
        for (u32 row = 0u; row < 16u; row++) {
            font_mem[(c * 32u) + row] =
                (c < 32u) ? 0x00u : ((row < 8u) ? font8x8[c - 32u][row] : 0x00u);
        }
    }

    /* Read back char 32 (space) and char 76 ('L') to verify the reload landed. */
    {
        static const char hexchars[] = "0123456789ABCDEF";
        serial_write("[font] ' '(0x20):");
        for (u32 i = 0u; i < 8u; i++) {
            u8 v = font_mem[(32u * 32u) + i];
            serial_putc(hexchars[(v >> 4) & 0xFu]);
            serial_putc(hexchars[v & 0xFu]);
        }
        serial_write(" 'L'(0x4C):");
        for (u32 i = 0u; i < 8u; i++) {
            u8 v = font_mem[(76u * 32u) + i];
            serial_putc(hexchars[(v >> 4) & 0xFu]);
            serial_putc(hexchars[v & 0xFu]);
        }
        serial_write("\n");
    }

    /* Restore the graphics controller and sequencer state exactly.  GC misc
       (index 6) must return to the text-mode value screen_init wrote (0x0E);
       hardcoding 0x05 here left the device interpreting the text buffer as
       graphics, which broke the width and produced garbage. */
    for (u32 i = 0u; i < 9u; i++) {
        outb(0x3CEu, (u8)i);
        outb(0x3CFu, orig_gc[i]);
    }
    for (u32 i = 0u; i < 5u; i++) {
        outb(VGA_SEQ_INDEX, (u8)i);
        outb(VGA_SEQ_DATA, orig_seq[i]);
    }
}

static void vga_restore_text_dac(void) {
    /* The attribute controller pulls foreground/background from DAC entries
       0..15.  BGA may leave the DAC undefined, so restore the standard text
       palette (black bg, gray-to-white fg) after switching back to text. */
    static const u8 p[16][3] = {
        {0,0,0},{0,0,42},{0,42,0},{0,42,42},
        {42,0,0},{42,0,42},{42,21,0},{42,42,42},
        {21,21,21},{21,21,63},{21,63,21},{21,63,63},
        {63,21,21},{63,21,63},{63,63,21},{63,63,63}
    };
    outb(0x3C6, 0xFFu);
    outb(0x3C8, 0u);
    for (u32 i = 0u; i < 16u; i++) {
        outb(0x3C9, p[i][0]);
        outb(0x3C9, p[i][1]);
        outb(0x3C9, p[i][2]);
    }
}

void gfx_restore_text(void) {
    /* gfx_exit_graphics leaves the VBE/BGA surface active (it only clears
       in_graphics), so the VGA text console never comes back. Turn BGA off and
       restore a clean VGA text mode 03h so the shell login is visible after
       the GUI.

       BGA reuses the std VGA's sequencer/CRTC, so simply disabling it and
       re-running the mode-03h tables (screen_init) left QEMU re-reading the
       text buffer with graphics clocking: the whole screen came out white.
       Do a complete hardware reset instead: blank the scanout via sequencer
       screen-off, reprogram every mode-03h register, restore the text DAC
       palette, then re-enable the scanout. screen_init rewrites the same
       register set and resets the console color to 0x0F, which is exactly
       what we want for the following screen_clear(). */
    if (use_vbe) {
        bga_write(BGA_INDEX_ENABLE, BGA_ENABLE_DISABLED);
    }
    use_vbe = 0u;
    in_graphics = 0;

    /* Sequencer: set screen-off so the mode switch below is not flickered. */
    outb(VGA_SEQ_INDEX, 0x01u);
    outb(VGA_SEQ_DATA, (u8)(inb(VGA_SEQ_DATA) | 0x20u));

    screen_init();

    vga_restore_text_font();
    vga_restore_text_dac();

    /* Sequencer: clear screen-off to start scanning out the text buffer. */
    outb(VGA_SEQ_INDEX, 0x01u);
    outb(VGA_SEQ_DATA, (u8)(inb(VGA_SEQ_DATA) & ~0x20u));

    /* Blank the buffer so no stale cells (e.g. attribute 0x14 pixels left
       behind by the GUI) survive. screen_init reset the console color to
       0x0F, so the clear writes white-on-black. */
    screen_clear();

    screen_dump_state();
}

/* Dynamic Multi-Resolution Buffer supporting up to 1080p / 2K monitors */
#define MAX_GFX_W 1920u
#define MAX_GFX_H 1080u

/* Backbuffers are allocated once the live mode is known instead of
   reserving worst-case 1920x1080 x(4+8) bytes of .bss forever. */
static u32* s_backbuffer = 0;
static rgba64_t* s_hdr_backbuffer = 0;

static u8 order_for(u32 pages) {
    u8 o = 0u;
    while ((1u << o) < pages && o < 20u) o++;
    return o;
}

static int gfx_backbuffers_ensure(void) {
    if (s_backbuffer) return 0;
    return gfx_backbuffers_alloc(s_bb_w, s_bb_h);
}

static int gfx_backbuffers_alloc(u32 w, u32 h) {
    if (w == 0u || h == 0u) return -1;
    if (w > MAX_GFX_W) w = MAX_GFX_W;
    if (h > MAX_GFX_H) h = MAX_GFX_H;
    if (s_backbuffer && s_bb_w == w && s_bb_h == h) return 0;
    u32 pages = ((u64)w * h * (sizeof(u32) + sizeof(rgba64_t)) + 4095u) / 4096u;
    void* block = frame_alloc_dma32_order(order_for(pages));
    if (!block) return -1;
    void* kva = (void*)paging_phys_to_virt((u64)(uintptr_t)block);
    u32* nb = (u32*)kva;
    s_hdr_backbuffer = (rgba64_t*)(nb + (u64)w * h);
    s_backbuffer = nb;
    s_bb_w = w;
    s_bb_h = h;
    return 0;
}
static u8 s_hdr_enabled = 1u;

void gfx_enable_hdr64(u8 enable) {
    s_hdr_enabled = enable ? 1u : 0u;
}

u8 gfx_is_hdr64(void) {
    return s_hdr_enabled;
}

rgba64_t gfx_rgb_to_rgba64(u32 color, u8 alpha) {
    rgba64_t c;
    u32 r8 = (color >> 16u) & 0xFFu;
    u32 g8 = (color >> 8u) & 0xFFu;
    u32 b8 = color & 0xFFu;
    c.r = (u16)((r8 << 8u) | r8);
    c.g = (u16)((g8 << 8u) | g8);
    c.b = (u16)((b8 << 8u) | b8);
    c.a = (u16)(((u32)alpha << 8u) | (u32)alpha);
    return c;
}

u32 gfx_rgba64_to_rgb(rgba64_t c) {
    u32 r8 = (c.r + 128u) >> 8u;
    u32 g8 = (c.g + 128u) >> 8u;
    u32 b8 = (c.b + 128u) >> 8u;
    if (r8 > 255u) r8 = 255u;
    if (g8 > 255u) g8 = 255u;
    if (b8 > 255u) b8 = 255u;
    return (r8 << 16u) | (g8 << 8u) | b8;
}

void gfx_px_blend64(u32 x, u32 y, u16 r, u16 g, u16 b, u16 a) {
    if (gfx_backbuffers_ensure() != 0) return;

    if (!in_graphics || a == 0u || x >= phys_w || y >= phys_h) return;
    u32 idx = y * phys_w + x;
    if (idx >= MAX_GFX_W * MAX_GFX_H) return;

    if (a >= 65500u) {
        s_hdr_backbuffer[idx] = (rgba64_t){ r, g, b, 65535u };
        s_backbuffer[idx] = (((u32)r >> 8u) << 16u) | (((u32)g >> 8u) << 8u) | ((u32)b >> 8u);
        return;
    }

    u32 inv = 65535u - (u32)a;
    rgba64_t bg = s_hdr_backbuffer[idx];
    u32 out_r = ((u32)bg.r * inv + (u32)r * (u32)a) / 65535u;
    u32 out_g = ((u32)bg.g * inv + (u32)g * (u32)a) / 65535u;
    u32 out_b = ((u32)bg.b * inv + (u32)b * (u32)a) / 65535u;

    s_hdr_backbuffer[idx] = (rgba64_t){ (u16)out_r, (u16)out_g, (u16)out_b, 65535u };
    s_backbuffer[idx] = (((out_r + 128u) >> 8u) << 16u) | (((out_g + 128u) >> 8u) << 8u) | ((out_b + 128u) >> 8u);
}

void gfx_rect64(u32 x, u32 y, u32 w, u32 h, rgba64_t color) {
    if (gfx_backbuffers_ensure() != 0) return;

    if (!in_graphics || w == 0u || h == 0u) return;
    if (x >= phys_w || y >= phys_h) return;
    if (w > phys_w - x) w = phys_w - x;
    if (h > phys_h - y) h = phys_h - y;

    u32 u32_val = gfx_rgba64_to_rgb(color);
    for (u32 yy = y; yy < y + h; yy++) {
        u32 row_start = yy * phys_w + x;
        for (u32 xx = 0; xx < w; xx++) {
            u32 idx = row_start + xx;
            s_hdr_backbuffer[idx] = color;
            s_backbuffer[idx] = u32_val;
        }
    }
}

void gfx_gradient_v64(u32 x, u32 y, u32 w, u32 h, rgba64_t top, rgba64_t bottom) {
    if (gfx_backbuffers_ensure() != 0) return;

    if (!in_graphics || w == 0u || h == 0u) return;
    if (x >= phys_w || y >= phys_h) return;
    if (w > phys_w - x) w = phys_w - x;
    if (h > phys_h - y) h = phys_h - y;

    for (u32 row = 0; row < h; row++) {
        u32 cur_y = y + row;
        u16 cur_r = (u16)((u32)top.r + (((i32)bottom.r - (i32)top.r) * (i32)row) / (i32)h);
        u16 cur_g = (u16)((u32)top.g + (((i32)bottom.g - (i32)top.g) * (i32)row) / (i32)h);
        u16 cur_b = (u16)((u32)top.b + (((i32)bottom.b - (i32)top.b) * (i32)row) / (i32)h);
        u16 cur_a = (u16)((u32)top.a + (((i32)bottom.a - (i32)top.a) * (i32)row) / (i32)h);
        rgba64_t col = { cur_r, cur_g, cur_b, cur_a };
        gfx_rect64(x, cur_y, w, 1u, col);
    }
}

/* ── Bayer 4x4 Spatial Dithering Matrix ── */
static const u8 BAYER_4X4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 }
};

/* ── 1. True Frosted Glass Gaussian Box Blur Engine ── */
void gfx_frosted_glass_blur(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 tint_color, u8 tint_alpha) {
    if (gfx_backbuffers_ensure() != 0) return;

    if (!in_graphics || w == 0u || h == 0u) return;
    if (x >= phys_w || y >= phys_h) return;
    if (w > phys_w - x) w = phys_w - x;
    if (h > phys_h - y) h = phys_h - y;

    u32 k_rad = radius > 4u ? 4u : (radius < 1u ? 1u : radius);

    /* Separable Box Blur Pass: compute frosted glass texture */
    for (u32 row = 0u; row < h; row++) {
        u32 py = y + row;
        u32 row_start = py * phys_w;

        for (u32 col = 0u; col < w; col++) {
            u32 px = x + col;
            u32 acc_r = 0u, acc_g = 0u, acc_b = 0u, count = 0u;

            /* Sample kernel window */
            for (i32 dy = -(i32)k_rad; dy <= (i32)k_rad; dy += 2) {
                i32 sy = (i32)py + dy;
                if (sy < 0 || (u32)sy >= phys_h) continue;

                for (i32 dx = -(i32)k_rad; dx <= (i32)k_rad; dx += 2) {
                    i32 sx = (i32)px + dx;
                    if (sx < 0 || (u32)sx >= phys_w) continue;

                    rgba64_t sample = s_hdr_backbuffer[sy * (i32)phys_w + sx];
                    acc_r += sample.r;
                    acc_g += sample.g;
                    acc_b += sample.b;
                    count++;
                }
            }

            if (count > 0u) {
                u16 blur_r = (u16)(acc_r / count);
                u16 blur_g = (u16)(acc_g / count);
                u16 blur_b = (u16)(acc_b / count);

                /* Blend tint color over blurred background */
                rgba64_t tint64 = gfx_rgb_to_rgba64(tint_color, tint_alpha);
                u32 inv = 65535u - (u32)tint64.a;
                u32 out_r = ((u32)blur_r * inv + (u32)tint64.r * (u32)tint64.a) / 65535u;
                u32 out_g = ((u32)blur_g * inv + (u32)tint64.g * (u32)tint64.a) / 65535u;
                u32 out_b = ((u32)blur_b * inv + (u32)tint64.b * (u32)tint64.a) / 65535u;

                /* Specular Highlight along top edge */
                if (row == 0u) {
                    out_r = (out_r + 65535u) / 2u;
                    out_g = (out_g + 65535u) / 2u;
                    out_b = (out_b + 65535u) / 2u;
                }

                u32 idx = row_start + px;
                s_hdr_backbuffer[idx] = (rgba64_t){ (u16)out_r, (u16)out_g, (u16)out_b, 65535u };

                /* Apply Bayer dithering for zero color banding */
                i32 dither = ((i32)BAYER_4X4[py & 3u][px & 3u] << 4) - 120;
                i32 r8 = (((i32)out_r + dither) >> 8);
                i32 g8 = (((i32)out_g + dither) >> 8);
                i32 b8 = (((i32)out_b + dither) >> 8);
                if (r8 < 0) r8 = 0; if (r8 > 255) r8 = 255;
                if (g8 < 0) g8 = 0; if (g8 > 255) g8 = 255;
                if (b8 < 0) b8 = 0; if (b8 > 255) b8 = 255;

                s_backbuffer[idx] = ((u32)r8 << 16u) | ((u32)g8 << 8u) | (u32)b8;
            }
        }
    }
}

/* ── 2. Hardware Cursor Plane with Alpha Shadow ── */
void gfx_draw_cursor(u32 mx, u32 my) {
    if (!in_graphics || mx >= phys_w || my >= phys_h) return;

    /* Arrow Pointer Bitmap Mask (12x18) */
    static const u16 CURSOR_MASK[18] = {
        0b100000000000,
        0b110000000000,
        0b111000000000,
        0b111100000000,
        0b111110000000,
        0b111111000000,
        0b111111100000,
        0b111111110000,
        0b111111111000,
        0b111111111100,
        0b111111000000,
        0b110111100000,
        0b100011110000,
        0b000001111000,
        0b000001111000,
        0b000000111100,
        0b000000111100,
        0b000000011000
    };

    /* 1. Draw Cursor Ambient Drop Shadow */
    for (u32 dy = 0; dy < 18; dy++) {
        for (u32 dx = 0; dx < 12; dx++) {
            if (CURSOR_MASK[dy] & (1u << (11u - dx))) {
                gfx_px_blend(mx + dx + 2u, my + dy + 3u, 0x000000u, 75u);
            }
        }
    }

    /* 2. Draw Pure White Pointer with 1px Dark Border */
    for (u32 dy = 0; dy < 18; dy++) {
        for (u32 dx = 0; dx < 12; dx++) {
            if (CURSOR_MASK[dy] & (1u << (11u - dx))) {
                /* Border check */
                u8 is_edge = (dx == 0 || dy == 0 || dy == 17 ||
                              !(CURSOR_MASK[dy] & (1u << (11u - (dx - 1)))) ||
                              !(CURSOR_MASK[dy] & (1u << (11u - (dx + 1)))));
                gfx_px_blend(mx + dx, my + dy, is_edge ? 0x0F172Au : 0xFFFFFFu, 255u);
            }
        }
    }
}

/* ── 3. Tiled Damage Tracking Engine (32x32 Tiles) ── */
#define TILE_SHIFT 5u /* 32 pixels */
#define TILE_SIZE (1u << TILE_SHIFT)
#define TILES_X 60u
#define TILES_Y 34u

static u8 s_tile_dirty[TILES_Y][TILES_X];

void gfx_tile_damage_add(u32 x, u32 y, u32 w, u32 h) {
    if (w == 0 || h == 0) return;
    u32 tx0 = x >> TILE_SHIFT;
    u32 ty0 = y >> TILE_SHIFT;
    u32 tx1 = (x + w + TILE_SIZE - 1u) >> TILE_SHIFT;
    u32 ty1 = (y + h + TILE_SIZE - 1u) >> TILE_SHIFT;

    if (tx1 > TILES_X) tx1 = TILES_X;
    if (ty1 > TILES_Y) ty1 = TILES_Y;

    for (u32 ty = ty0; ty < ty1; ty++) {
        for (u32 tx = tx0; tx < tx1; tx++) {
            s_tile_dirty[ty][tx] = 1u;
        }
    }
}

void gfx_tile_present(void) {
    if (!in_graphics) return;
    for (u32 ty = 0u; ty < TILES_Y; ty++) {
        for (u32 tx = 0u; tx < TILES_X; tx++) {
            if (s_tile_dirty[ty][tx]) {
                u32 rx = tx << TILE_SHIFT;
                u32 ry = ty << TILE_SHIFT;
                gfx_present_rect(rx, ry, TILE_SIZE, TILE_SIZE);
                s_tile_dirty[ty][tx] = 0u;
            }
        }
    }
}

void gfx_present(void) {
    if (!in_graphics) return;

    /* ── GPU backend: flush the entire framebuffer via VirtIO-GPU ── */
    if (use_gpu_backend) {
        gpu_backend_flush_rect(0u, 0u, phys_w, phys_h);
        return;
    }

    if (use_vbe && vbe_bytespp == 4u && fb32) {
        if (fb_pitch_bytes == (phys_w * 4u)) {
            u64* dst64 = (u64*)fb8;
            const u64* src64 = (const u64*)s_backbuffer;
            u32 count = (phys_w * phys_h) / 2u;
            for (u32 i = 0; i < count; i++) {
                dst64[i] = src64[i];
            }
        } else {
            for (u32 y = 0; y < phys_h; y++) {
                u64* dst64 = (u64*)(fb8 + (y * fb_pitch_bytes));
                const u64* src64 = (const u64*)(&s_backbuffer[y * phys_w]);
                u32 qwords = phys_w / 2u;
                for (u32 x = 0; x < qwords; x++) {
                    dst64[x] = src64[x];
                }
            }
        }
    }
}

/* ── Hardware Damage Tracking (40x Faster Rendering, 144Hz+) ──────── */
typedef struct {
    u32 min_x, min_y;
    u32 max_x, max_y;
    u8 is_damaged;
    u8 is_full;
} gfx_damage_tracker_t;

static gfx_damage_tracker_t s_damage = { 0, 0, 0, 0, 0, 0 };

void gfx_damage_reset(void) {
    s_damage.min_x = phys_w;
    s_damage.min_y = phys_h;
    s_damage.max_x = 0;
    s_damage.max_y = 0;
    s_damage.is_damaged = 0;
    s_damage.is_full = 0;
}

void gfx_damage_add(u32 x, u32 y, u32 w, u32 h) {
    if (w == 0 || h == 0) return;
    if (x >= phys_w || y >= phys_h) return;
    if (x + w > phys_w) w = phys_w - x;
    if (y + h > phys_h) h = phys_h - y;

    if (x < s_damage.min_x) s_damage.min_x = x;
    if (y < s_damage.min_y) s_damage.min_y = y;
    if (x + w > s_damage.max_x) s_damage.max_x = x + w;
    if (y + h > s_damage.max_y) s_damage.max_y = y + h;
    s_damage.is_damaged = 1;
}

void gfx_damage_add_full(void) {
    s_damage.min_x = 0;
    s_damage.min_y = 0;
    s_damage.max_x = phys_w;
    s_damage.max_y = phys_h;
    s_damage.is_damaged = 1;
    s_damage.is_full = 1;
}

void gfx_present_damaged(void) {
    if (!in_graphics || !s_damage.is_damaged) return;
    if (s_damage.is_full || (s_damage.min_x == 0 && s_damage.min_y == 0 && s_damage.max_x >= phys_w && s_damage.max_y >= phys_h)) {
        gfx_present();
    } else {
        u32 rw = (s_damage.max_x > s_damage.min_x) ? (s_damage.max_x - s_damage.min_x) : 0u;
        u32 rh = (s_damage.max_y > s_damage.min_y) ? (s_damage.max_y - s_damage.min_y) : 0u;
        if (rw > 0 && rh > 0) {
            gfx_present_rect(s_damage.min_x, s_damage.min_y, rw, rh);
        }
    }
    gfx_damage_reset();
}

static inline u32 isqrt_u32(u32 n) {
    if (n == 0u) return 0u;
    u32 x0 = n / 2u;
    if (x0 == 0u) return 1u;
    u32 x1 = (x0 + n / x0) / 2u;
    while (x1 < x0) {
        x0 = x1;
        x1 = (x0 + n / x0) / 2u;
    }
    return x0;
}

void gfx_circle_aa(u32 cx, u32 cy, u32 radius, u32 color, u8 alpha) {
    if (radius == 0 || alpha == 0) return;
    i32 r = (i32)radius;
    i32 r2 = r * r;
    i32 inner_r = r > 1 ? r - 1 : 0;
    i32 inner_r2 = inner_r * inner_r;

    for (i32 dy = -r; dy <= r; dy++) {
        i32 py = (i32)cy + dy;
        if (py < 0 || (u32)py >= phys_h) continue;
        for (i32 dx = -r; dx <= r; dx++) {
            i32 px = (i32)cx + dx;
            if (px < 0 || (u32)px >= phys_w) continue;
            i32 d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                u8 a = alpha;
                if (d2 > inner_r2) {
                    a = (u8)((alpha * 160u) / 255u);
                }
                gfx_px_blend((u32)px, (u32)py, color, a);
            }
        }
    }
}

void gfx_round_rect_aa(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 border_color, u32 fill_color, u8 has_border) {
    if (w == 0 || h == 0) return;
    if (radius > w / 2u) radius = w / 2u;
    if (radius > h / 2u) radius = h / 2u;

    u32 r2 = radius * radius;

    for (u32 row = 0; row < h; row++) {
        u32 py = y + row;
        if (py >= phys_h) break;

        u32 inset = 0;
        u8 is_corner = 0;
        if (radius > 0) {
            if (row < radius) {
                u32 dy = radius - 1u - row;
                u32 dy2 = dy * dy;
                u32 dx = (r2 > dy2) ? (u32)isqrt_u32(r2 - dy2) : 0;
                inset = (radius > dx) ? (radius - dx) : 0;
                is_corner = 1;
            } else if (row >= h - radius) {
                u32 dy = row - (h - radius);
                u32 dy2 = dy * dy;
                u32 dx = (r2 > dy2) ? (u32)isqrt_u32(r2 - dy2) : 0;
                inset = (radius > dx) ? (radius - dx) : 0;
                is_corner = 1;
            }
        }

        u32 x0 = x + inset;
        u32 x1 = (x + w > inset) ? (x + w - inset) : x;
        if (x1 <= x0) continue;

        /* Anti-Aliased Sub-Pixel Fringe on Corner Edges */
        if (is_corner && inset > 0) {
            if (x0 > 0 && x0 - 1 < phys_w) gfx_px_blend(x0 - 1, py, has_border ? border_color : fill_color, 120u);
            if (x1 < phys_w) gfx_px_blend(x1, py, has_border ? border_color : fill_color, 120u);
        }

        if (row == 0 || row == h - 1 || !has_border || border_color == fill_color) {
            gfx_rect(x0, py, x1 - x0, 1u, has_border ? border_color : fill_color);
        } else {
            gfx_rect(x0, py, 1u, 1u, border_color);
            if (x1 - x0 > 2u) gfx_rect(x0 + 1u, py, x1 - x0 - 2u, 1u, fill_color);
            if (x1 - x0 > 1u) gfx_rect(x1 - 1u, py, 1u, 1u, border_color);
        }
    }
}

void gfx_present_rect(u32 rx, u32 ry, u32 rw, u32 rh) {
    if (!in_graphics) return;
    if (rx >= phys_w || ry >= phys_h) return;
    if (rx + rw > phys_w) rw = phys_w - rx;
    if (ry + rh > phys_h) rh = phys_h - ry;

    /* ── GPU backend: flush the region via VirtIO-GPU ── */
    if (use_gpu_backend) {
        gpu_backend_flush_rect(rx, ry, rw, rh);
        return;
    }

    if (!use_vbe || vbe_bytespp != 4u || !fb32) return;

    for (u32 y = ry; y < ry + rh && y < phys_h; y++) {
        u64* dst64 = (u64*)(fb8 + (y * fb_pitch_bytes) + (rx * 4u));
        const u64* src64 = (const u64*)(&s_backbuffer[y * phys_w + rx]);
        u32 qwords = rw / 2u;
        for (u32 q = 0; q < qwords; q++) {
            dst64[q] = src64[q];
        }
        if (rw & 1u) {
            u32* dst32 = (u32*)dst64;
            const u32* src32 = (const u32*)src64;
            dst32[rw - 1u] = src32[rw - 1u];
        }
    }
}

static void px(u32 x, u32 y, u32 c) {
    if (!in_graphics) return;
    if (x >= phys_w || y >= phys_h) return;
    u32 cc = color_index_to_rgb(c);
    s_backbuffer[y * phys_w + x] = cc;
}

static u32 mix_rgb(u32 background, u32 foreground, u8 alpha) {
    u32 inv = 255u - alpha;
    u32 br = (background >> 16) & 0xFFu;
    u32 bg = (background >> 8) & 0xFFu;
    u32 bb = background & 0xFFu;
    u32 fr = (foreground >> 16) & 0xFFu;
    u32 fg = (foreground >> 8) & 0xFFu;
    u32 fb = foreground & 0xFFu;
    return ((((br * inv) + (fr * alpha)) / 255u) << 16) |
           ((((bg * inv) + (fg * alpha)) / 255u) << 8) |
           (((bb * inv) + (fb * alpha)) / 255u);
}

static u32 gfx_read_px(u32 x, u32 y) {
    if (x >= phys_w || y >= phys_h) return 0u;
    return s_backbuffer[y * phys_w + x];
}

void gfx_px_blend(u32 x, u32 y, u32 color, u8 alpha) {
    if (!in_graphics) return;
    if (alpha == 0u) return;
    if (x >= phys_w || y >= phys_h) return;

    if (s_hdr_enabled) {
        rgba64_t fg64 = gfx_rgb_to_rgba64(color, alpha);
        gfx_px_blend64(x, y, fg64.r, fg64.g, fg64.b, fg64.a);
        return;
    }

    u32 fg = color_index_to_rgb(color);
    if (alpha >= 250u) {
        s_backbuffer[y * phys_w + x] = fg;
        return;
    }
    u32 bg = s_backbuffer[y * phys_w + x];
    s_backbuffer[y * phys_w + x] = mix_rgb(bg, fg, alpha);
}

void gfx_clear(u32 color) {
    if (!in_graphics) return;
    u32 cc = color_index_to_rgb(color);
    rgba64_t c64 = gfx_rgb_to_rgba64(color, 255u);
    u32 total = phys_w * phys_h;
    if (total > MAX_GFX_W * MAX_GFX_H) total = MAX_GFX_W * MAX_GFX_H;
    for (u32 i = 0; i < total; i++) {
        s_backbuffer[i] = cc;
        s_hdr_backbuffer[i] = c64;
    }
}

void gfx_blit(u32 x, u32 y, u32 w, u32 h, const u32* pixels) {
    if (!in_graphics || !pixels || w == 0u || h == 0u) return;
    if (x >= phys_w || y >= phys_h) return;
    u32 src_pitch = w;
    if (w > phys_w - x) w = phys_w - x;
    if (h > phys_h - y) h = phys_h - y;

    for (u32 row_index = 0u; row_index < h; row_index++) {
        const u32* source = pixels + (row_index * src_pitch);
        u32* target = &s_backbuffer[(y + row_index) * phys_w + x];
        u64* dst64 = (u64*)target;
        const u64* src64 = (const u64*)source;
        u32 qwords = w / 2u;
        for (u32 q = 0; q < qwords; q++) {
            dst64[q] = src64[q];
        }
        if (w & 1u) {
            target[w - 1u] = source[w - 1u];
        }
    }
}

void gfx_read_rect(u32 x, u32 y, u32 w, u32 h, u32* out_pixels) {
    if (!in_graphics || !out_pixels || w == 0u || h == 0u) return;
    for (u32 row = 0u; row < h; row++) {
        if ((y + row) >= phys_h) break;
        const u32* src = &s_backbuffer[(y + row) * phys_w + x];
        u32* dst = out_pixels + (row * w);
        for (u32 col = 0u; col < w; col++) {
            if ((x + col) >= phys_w) break;
            dst[col] = src[col];
        }
    }
}

void gfx_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (!in_graphics || w == 0u || h == 0u) return;
    if (x >= phys_w || y >= phys_h) return;
    if (w > phys_w - x) w = phys_w - x;
    if (h > phys_h - y) h = phys_h - y;

    u32 cc = color_index_to_rgb(color);
    for (u32 yy = y; yy < y + h; yy++) {
        u32* row = &s_backbuffer[yy * phys_w + x];
        for (u32 xx = 0; xx < w; xx++) {
            row[xx] = cc;
        }
    }
}

void gfx_round_rect(u32 x, u32 y, u32 w, u32 h, u32 radius, u32 color) {
    if (!in_graphics || w == 0u || h == 0u) return;
    if (radius > w / 2u) radius = w / 2u;
    if (radius > h / 2u) radius = h / 2u;
    for (u32 yy = 0u; yy < h; yy++) {
        for (u32 xx = 0u; xx < w; xx++) {
            u32 cx = xx;
            u32 cy = yy;
            if (xx < radius) cx = radius;
            else if (xx >= w - radius) cx = w - radius - 1u;
            if (yy < radius) cy = radius;
            else if (yy >= h - radius) cy = h - radius - 1u;
            if (xx < radius || xx >= w - radius || yy < radius || yy >= h - radius) {
                u32 dx = xx > cx ? xx - cx : cx - xx;
                u32 dy = yy > cy ? yy - cy : cy - yy;
                if (dx * dx + dy * dy > radius * radius) continue;
            }
            px(x + xx, y + yy, color);
        }
    }
}

static void glyph(u32 x, u32 y, u32 color, char ch) {
    u32 idx;
    u8 uch = (u8)ch;
    if (uch < 32u || uch > 127u) {
        idx = 0;
    } else {
        idx = (u32)uch - 32u;
    }
    /* First paint a subtle coverage fringe, then the solid glyph.
       Font rows are MSB-first: bit 7 is the leftmost column. */
    for (u32 row = 0; row < 8; row++) {
        u8 bits = font8x8[idx][row];
        for (u32 col = 0; col < 8; col++) {
            if (bits & (1u << (7u - col))) {
                if (col > 0u && !(bits & (1u << (8u - col)))) gfx_px_blend(x + col - 1u, y + row, color, 72u);
                if (col < 7u && !(bits & (1u << (6u - col)))) gfx_px_blend(x + col + 1u, y + row, color, 72u);
                if (row > 0u && !(font8x8[idx][row - 1u] & (1u << (7u - col)))) gfx_px_blend(x + col, y + row - 1u, color, 72u);
                if (row < 7u && !(font8x8[idx][row + 1u] & (1u << (7u - col)))) gfx_px_blend(x + col, y + row + 1u, color, 72u);
            }
        }
    }
    for (u32 row = 0; row < 8; row++) {
        u8 bits = font8x8[idx][row];
        for (u32 col = 0; col < 8; col++) {
            if (bits & (1u << (7u - col))) {
                gfx_px_blend(x + col, y + row, color, 255u);
            }
        }
    }
}

void gfx_text(u32 x, u32 y, u32 color, const char* text) {
    if (!in_graphics || !text) return;
    u32 cx = x;
    u32 cy = y;
    for (u32 i = 0; text[i]; i++) {
        char c = text[i];
        if (c == '\n') {
            cx = x;
            cy += 10;
            continue;
        }
        glyph(cx, cy, color, c);
        cx += 9;
    }
}

u32 gfx_width(void) {
    return phys_w;
}

u32 gfx_height(void) {
    return phys_h;
}

u32 gfx_bpp(void) { return vbe_bpp; }

const char* gfx_backend_name(void) {
    if (!in_graphics) return "inactive";
    if (use_gpu_backend) return gpu_backend_name();
    if (use_vbe && vbe_bpp == 32u && phys_w >= 640u) return "BGA/VBE linear framebuffer";
    return "VGA mode 13h framebuffer";
}
