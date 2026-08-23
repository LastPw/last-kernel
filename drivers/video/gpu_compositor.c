#include "gpu_compositor.h"
#include "gpu_backend.h"
#include "gfx.h"
#include "serial.h"
#include "string.h"

/* ── State ── */
static compositor_layer_t g_layers[COMP_MAX_LAYERS];
static u32 g_layer_count;
static u8 g_initialized;

/* ── Global damage (union of all layer damages) ── */
static layer_damage_t g_global_damage;

/* ── Helpers ── */

static inline u32 min_u32(u32 a, u32 b) { return a < b ? a : b; }
static inline u32 max_u32(u32 a, u32 b) { return a > b ? a : b; }

/* Alpha-blend a single ARGB pixel over a destination pixel.
   src is premultiplied ARGB (A in bits 24-31, R in 16-23, G in 8-15, B in 0-7). */
static inline u32 blend_pixel(u32 dst, u32 src) {
    u32 sa = (src >> 24) & 0xFFu;
    if (sa == 0u) return dst;
    if (sa == 255u) return src;
    u32 da = (dst >> 24) & 0xFFu;
    u32 sr = (src >> 16) & 0xFFu;
    u32 sg = (src >> 8)  & 0xFFu;
    u32 sb = src & 0xFFu;
    u32 dr = (dst >> 16) & 0xFFu;
    u32 dg = (dst >> 8)  & 0xFFu;
    u32 db = dst & 0xFFu;
    u32 inv_a = 255u - sa;
    u32 out_r = (sr * sa + dr * inv_a) / 255u;
    u32 out_g = (sg * sa + dg * inv_a) / 255u;
    u32 out_b = (sb * sa + db * inv_a) / 255u;
    u32 out_a = sa + (da * inv_a) / 255u;
    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

/* ── Initialization ── */

void gpu_compositor_init(void) {
    memset(g_layers, 0, sizeof(g_layers));
    g_layer_count = 0u;
    g_initialized = 1u;
    memset(&g_global_damage, 0, sizeof(g_global_damage));
    serial_writeln("[Compositor] initialized");
}

void gpu_compositor_shutdown(void) {
    for (u32 i = 0; i < COMP_MAX_LAYERS; i++) {
        if (g_layers[i].active && g_layers[i].pixels) {
            /* In a real OS we'd free the pixel buffer */
            g_layers[i].pixels = 0;
        }
    }
    memset(g_layers, 0, sizeof(g_layers));
    g_layer_count = 0u;
    g_initialized = 0u;
    serial_writeln("[Compositor] shutdown");
}

/* ── Layer Management ── */

int gpu_compositor_create_layer(const char* name, layer_type_t type,
                                 u32 x, u32 y, u32 width, u32 height,
                                 u32 z_order) {
    if (!g_initialized) return -1;
    if (width == 0u || height == 0u) return -1;

    /* Find a free slot */
    int slot = -1;
    for (u32 i = 0; i < COMP_MAX_LAYERS; i++) {
        if (!g_layers[i].active) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        serial_writeln("[Compositor] no free layer slots");
        return -1;
    }

    /* Allocate pixel buffer (ARGB8888) */
    u32* pixels = (u32*)0; /* placeholder — in a real OS, alloc from heap */
    /* For now, use a static pool for common sizes */
    static u32 layer_pool[1024 * 768 * 4]; /* enough for a few layers */
    static u32 pool_offset = 0u;
    u32 needed = width * height;
    if (pool_offset + needed <= sizeof(layer_pool) / sizeof(u32)) {
        pixels = &layer_pool[pool_offset];
        pool_offset += needed;
    } else {
        serial_writeln("[Compositor] layer pixel pool exhausted");
        return -1;
    }

    compositor_layer_t* layer = &g_layers[slot];
    layer->active = 1;
    layer->type = type;
    layer->x = x;
    layer->y = y;
    layer->width = width;
    layer->height = height;
    layer->z_order = z_order;
    layer->visible = 1;
    layer->needs_composite = 1;
    layer->pixels = pixels;
    memset(&layer->damage, 0, sizeof(layer->damage));
    layer->damage.is_damaged = 1; /* full damage on creation */

    /* Copy name */
    u32 name_len = 0;
    while (name[name_len] && name_len < COMP_MAX_NAME - 1u) {
        layer->name[name_len] = name[name_len];
        name_len++;
    }
    layer->name[name_len] = '\0';

    g_layer_count++;

    serial_write("[Compositor] created layer #");
    serial_write_dec((u32)slot);
    serial_write(" '");
    serial_write(name);
    serial_write("' ");
    serial_write_dec(width);
    serial_write("x");
    serial_write_dec(height);
    serial_write(" z=");
    serial_write_dec(z_order);
    serial_writeln("");
    return slot;
}

void gpu_compositor_destroy_layer(int layer_id) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return;
    if (!g_layers[layer_id].active) return;

    g_layers[layer_id].active = 0;
    g_layers[layer_id].pixels = 0;
    if (g_layer_count > 0u) g_layer_count--;

    serial_write("[Compositor] destroyed layer #");
    serial_write_dec((u32)layer_id);
    serial_writeln("");
}

void gpu_compositor_move_layer(int layer_id, u32 x, u32 y) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return;
    if (!g_layers[layer_id].active) return;

    compositor_layer_t* layer = &g_layers[layer_id];
    /* Mark both old and new positions as damaged */
    gpu_compositor_layer_damage_full(layer_id);
    layer->x = x;
    layer->y = y;
}

int gpu_compositor_resize_layer(int layer_id, u32 new_width, u32 new_height) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return -1;
    if (!g_layers[layer_id].active) return -1;
    if (new_width == 0u || new_height == 0u) return -1;

    compositor_layer_t* layer = &g_layers[layer_id];

    /* For now, only allow shrink (can't easily realloc from static pool) */
    if (new_width > layer->width || new_height > layer->height) return -1;

    layer->width = new_width;
    layer->height = new_height;
    gpu_compositor_layer_damage_full(layer_id);
    return 0;
}

void gpu_compositor_set_visible(int layer_id, u8 visible) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return;
    if (!g_layers[layer_id].active) return;

    u8 was_visible = g_layers[layer_id].visible;
    g_layers[layer_id].visible = visible;
    if (was_visible != visible) {
        /* Visibility change requires full recomposite of affected region */
        gpu_compositor_layer_damage_full(layer_id);
    }
}

void gpu_compositor_set_z_order(int layer_id, u32 z_order) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return;
    if (!g_layers[layer_id].active) return;

    g_layers[layer_id].z_order = z_order;
    gpu_compositor_layer_damage_full(layer_id);
}

u32* gpu_compositor_layer_pixels(int layer_id) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return 0;
    if (!g_layers[layer_id].active) return 0;
    return g_layers[layer_id].pixels;
}

void gpu_compositor_layer_size(int layer_id, u32* width, u32* height) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) {
        if (width) *width = 0;
        if (height) *height = 0;
        return;
    }
    if (width) *width = g_layers[layer_id].width;
    if (height) *height = g_layers[layer_id].height;
}

void gpu_compositor_layer_pos(int layer_id, u32* x, u32* y) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    if (x) *x = g_layers[layer_id].x;
    if (y) *y = g_layers[layer_id].y;
}

void gpu_compositor_layer_damage(int layer_id, u32 x, u32 y, u32 w, u32 h) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return;
    if (!g_layers[layer_id].active) return;

    compositor_layer_t* layer = &g_layers[layer_id];
    if (w == 0u || h == 0u) return;
    if (x >= layer->width || y >= layer->height) return;
    if (x + w > layer->width) w = layer->width - x;
    if (y + h > layer->height) h = layer->height - y;

    layer_damage_t* d = &layer->damage;
    if (!d->is_damaged) {
        d->min_x = x;
        d->min_y = y;
        d->max_x = x + w;
        d->max_y = y + h;
        d->is_damaged = 1;
    } else {
        if (x < d->min_x) d->min_x = x;
        if (y < d->min_y) d->min_y = y;
        if (x + w > d->max_x) d->max_x = x + w;
        if (y + h > d->max_y) d->max_y = y + h;
    }
    layer->needs_composite = 1;
}

void gpu_compositor_layer_damage_full(int layer_id) {
    if (layer_id < 0 || (u32)layer_id >= COMP_MAX_LAYERS) return;
    if (!g_layers[layer_id].active) return;

    compositor_layer_t* layer = &g_layers[layer_id];
    layer->damage.min_x = 0;
    layer->damage.min_y = 0;
    layer->damage.max_x = layer->width;
    layer->damage.max_y = layer->height;
    layer->damage.is_damaged = 1;
    layer->needs_composite = 1;
}

/* ── Compositing ── */

void gpu_compositor_present(void) {
    if (!g_initialized) return;

    u32 screen_w = gpu_backend_width();
    u32 screen_h = gpu_backend_height();
    u32* fb = gpu_backend_framebuffer();
    if (!fb) return;

    /* Find the union of all damaged regions across all layers */
    u32 damage_min_x = screen_w;
    u32 damage_min_y = screen_h;
    u32 damage_max_x = 0;
    u32 damage_max_y = 0;
    u8  any_damage = 0;

    for (u32 i = 0; i < COMP_MAX_LAYERS; i++) {
        compositor_layer_t* layer = &g_layers[i];
        if (!layer->active || !layer->visible || !layer->needs_composite) continue;
        if (!layer->damage.is_damaged) continue;

        /* Map layer damage to screen coordinates */
        u32 sx = layer->x + layer->damage.min_x;
        u32 sy = layer->y + layer->damage.min_y;
        u32 sw = layer->damage.max_x - layer->damage.min_x;
        u32 sh = layer->damage.max_y - layer->damage.min_y;

        /* Clip to screen */
        if (sx < damage_min_x) damage_min_x = sx;
        if (sy < damage_min_y) damage_min_y = sy;
        if (sx + sw > damage_max_x) damage_max_x = sx + sw;
        if (sy + sh > damage_max_y) damage_max_y = sy + sh;
        any_damage = 1;
    }

    if (!any_damage) return;

    /* Clamp to screen bounds */
    if (damage_max_x > screen_w) damage_max_x = screen_w;
    if (damage_max_y > screen_h) damage_max_y = screen_h;

    /* Collect visible layers sorted by z-order (insertion sort, tiny N) */
    int sorted[COMP_MAX_LAYERS];
    int sorted_count = 0;
    for (u32 i = 0; i < COMP_MAX_LAYERS; i++) {
        if (!g_layers[i].active || !g_layers[i].visible) continue;
        /* Insert sorted by z_order ascending */
        int j = sorted_count;
        while (j > 0 && g_layers[sorted[j-1]].z_order > g_layers[i].z_order) {
            sorted[j] = sorted[j-1];
            j--;
        }
        sorted[j] = (int)i;
        sorted_count++;
    }

    /* Composite: walk the damaged region, for each pixel, blend all layers */
    u32 fb_pitch = gpu_backend_pitch() / 4u; /* pixels per row */

    for (u32 sy = damage_min_y; sy < damage_max_y; sy++) {
        for (u32 sx = damage_min_x; sx < damage_max_x; sx++) {
            /* Start with transparent black */
            u32 result = 0x00000000u;

            /* Blend each layer back-to-front */
            for (int li = 0; li < sorted_count; li++) {
                compositor_layer_t* layer = &g_layers[sorted[li]];

                /* Check if this pixel is within the layer bounds */
                i32 lx = (i32)sx - (i32)layer->x;
                i32 ly = (i32)sy - (i32)layer->y;
                if (lx < 0 || ly < 0) continue;
                if ((u32)lx >= layer->width || (u32)ly >= layer->height) continue;

                u32 src_pixel = layer->pixels[ly * (i32)layer->width + lx];
                result = blend_pixel(result, src_pixel);
            }

            /* Write to framebuffer (opaque write since we started with 0) */
            fb[sy * fb_pitch + sx] = result;
        }
    }

    /* Flush the damaged region to the display */
    u32 rw = damage_max_x - damage_min_x;
    u32 rh = damage_max_y - damage_min_y;
    if (rw > 0u && rh > 0u) {
        /* Use the backend's damage tracking to flush just this region */
        gpu_backend_damage_add(damage_min_x, damage_min_y, rw, rh);
        gpu_backend_present_damaged();
    }

    /* Clear per-layer damage flags */
    for (u32 i = 0; i < COMP_MAX_LAYERS; i++) {
        if (g_layers[i].active) {
            g_layers[i].damage.is_damaged = 0;
            g_layers[i].needs_composite = 0;
        }
    }
}

void gpu_compositor_present_full(void) {
    if (!g_initialized) return;

    /* Mark all visible layers as fully damaged */
    for (u32 i = 0; i < COMP_MAX_LAYERS; i++) {
        if (g_layers[i].active && g_layers[i].visible) {
            gpu_compositor_layer_damage_full((int)i);
        }
    }
    gpu_compositor_present();
}

u32 gpu_compositor_layer_count(void) {
    return g_layer_count;
}
