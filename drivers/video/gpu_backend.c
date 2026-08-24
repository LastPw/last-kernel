#include "gpu_backend.h"
#include "virtio_gpu.h"
#include "gfx.h"
#include "serial.h"
#include "string.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  GPU Backend Abstraction
 *
 *  Routes gfx_* operations to either:
 *    - VirtIO-GPU (hardware scanout, DMA-based)
 *    - CPU framebuffer (BGA/Limine fallback)
 * ═══════════════════════════════════════════════════════════════════════ */

static gpu_backend_type_t g_backend = GPU_BACKEND_NONE;

/* ── CPU backend framebuffer ── */
#define CPU_FB_MAX_W  1280u
#define CPU_FB_MAX_H  800u
static u32 g_cpu_fb[CPU_FB_MAX_W * CPU_FB_MAX_H];
static u32 g_cpu_width;
static u32 g_cpu_height;
static u32 g_cpu_pitch;

/* ── Damage tracking ── */
static struct {
    u32 min_x, min_y, max_x, max_y;
    u8  is_damaged;
} g_damage;

/* ── GPU backend init ── */
gpu_backend_type_t gpu_backend_init(void) {
    memset(&g_damage, 0, sizeof(g_damage));

    if (virtio_gpu_init() == 0 && virtio_gpu_available()) {
        g_backend = GPU_BACKEND_GPU;
        serial_write("[GPU] VirtIO-GPU: ");
        serial_write_dec(virtio_gpu_width());
        serial_write("x");
        serial_write_dec(virtio_gpu_height());
        serial_write(" back_buf=");
        serial_write_hex((u32)(u64)virtio_gpu_back_buffer());
        serial_writeln("");
        return g_backend;
    }

    g_backend = GPU_BACKEND_CPU;
    g_cpu_width = 1024u;
    g_cpu_height = 768u;
    g_cpu_pitch = g_cpu_width * 4u;
    memset(g_cpu_fb, 0, sizeof(g_cpu_fb));
    serial_writeln("[GPU] falling back to CPU framebuffer");
    return g_backend;
}

void gpu_backend_set_cpu_params(u32 width, u32 height, u32 pitch) {
    g_cpu_width = width;
    g_cpu_height = height;
    g_cpu_pitch = pitch;
}

gpu_backend_type_t gpu_backend_type(void) { return g_backend; }
int gpu_backend_is_gpu(void) { return g_backend == GPU_BACKEND_GPU ? 1 : 0; }

const char* gpu_backend_name(void) {
    switch (g_backend) {
        case GPU_BACKEND_GPU: return virtio_gpu_backend_name();
        case GPU_BACKEND_CPU: return "CPU framebuffer";
        default: return "inactive";
    }
}

/* ── Framebuffer Access ── */

u32* gpu_backend_framebuffer(void) {
    if (g_backend == GPU_BACKEND_GPU) {
        return (u32*)virtio_gpu_back_buffer();
    }
    return g_cpu_fb;
}

u32 gpu_backend_width(void) {
    return (g_backend == GPU_BACKEND_GPU) ? virtio_gpu_width() : g_cpu_width;
}
u32 gpu_backend_height(void) {
    return (g_backend == GPU_BACKEND_GPU) ? virtio_gpu_height() : g_cpu_height;
}
u32 gpu_backend_pitch(void) {
    return (g_backend == GPU_BACKEND_GPU) ? virtio_gpu_framebuffer_pitch() : g_cpu_pitch;
}
u32 gpu_backend_bpp(void) { return 4u; }
u32 gpu_backend_fb_size(void) {
    return (g_backend == GPU_BACKEND_GPU) ? virtio_gpu_framebuffer_size()
                                          : g_cpu_pitch * g_cpu_height;
}

/* ── Damage Tracking ── */

void gpu_backend_damage_add(u32 x, u32 y, u32 w, u32 h) {
    if (w == 0u || h == 0u) return;
    u32 fb_w = gpu_backend_width();
    u32 fb_h = gpu_backend_height();
    if (x >= fb_w || y >= fb_h) return;
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;

    if (!g_damage.is_damaged) {
        g_damage.min_x = x; g_damage.min_y = y;
        g_damage.max_x = x + w; g_damage.max_y = y + h;
        g_damage.is_damaged = 1;
    } else {
        if (x < g_damage.min_x) g_damage.min_x = x;
        if (y < g_damage.min_y) g_damage.min_y = y;
        if (x + w > g_damage.max_x) g_damage.max_x = x + w;
        if (y + h > g_damage.max_y) g_damage.max_y = y + h;
    }
}

void gpu_backend_damage_add_full(void) {
    g_damage.min_x = 0; g_damage.min_y = 0;
    g_damage.max_x = gpu_backend_width();
    g_damage.max_y = gpu_backend_height();
    g_damage.is_damaged = 1;
}

void gpu_backend_damage_reset(void) { g_damage.is_damaged = 0; }
int gpu_backend_has_damage(void) { return g_damage.is_damaged ? 1 : 0; }

/* ── Presentation ── */

void gpu_backend_flush_rect(u32 x, u32 y, u32 w, u32 h) {
    if (g_backend == GPU_BACKEND_GPU) {
        virtio_gpu_flush_rect(x, y, w, h);
    }
}

void gpu_backend_present_full(void) {
    if (g_backend == GPU_BACKEND_GPU) {
        virtio_gpu_flush_rect(0u, 0u, gpu_backend_width(), gpu_backend_height());
    }
}

void gpu_backend_present_damaged(void) {
    if (!g_damage.is_damaged) return;
    u32 rw = g_damage.max_x - g_damage.min_x;
    u32 rh = g_damage.max_y - g_damage.min_y;
    if (g_backend == GPU_BACKEND_GPU && rw > 0u && rh > 0u) {
        virtio_gpu_flush_rect(g_damage.min_x, g_damage.min_y, rw, rh);
    }
    gpu_backend_damage_reset();
}

int gpu_backend_page_flip(void) {
    if (g_backend == GPU_BACKEND_GPU) {
        return virtio_gpu_page_flip();
    }
    return -1;
}
