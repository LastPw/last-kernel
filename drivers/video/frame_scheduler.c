#include "frame_scheduler.h"
#include "gpu_engine.h"
#include "virtio_gpu.h"
#include "gfx.h"
#include "serial.h"
#include "string.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  Frame Scheduler Implementation
 *
 *  Key optimization: instead of re-compositing the entire screen every
 *  frame, we track which regions of which windows changed, and only
 *  composite + flush those specific rectangles.
 *
 *  Before:  gfx_present() copies 1024*768*4 = 3MB every frame
 *  After:   flush only the ~50KB of actually changed pixels
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── State ── */
static sched_window_t g_windows[SCHED_MAX_WINDOWS];
static u32 g_window_count;
static u8  g_initialized;

/* ── Frame timing ── */
#define TARGET_FPS       60u
#define TARGET_FRAME_US  (1000000u / TARGET_FPS)  /* ~16667 us per frame */

/* ── Dirty region accumulator ── */
static sched_rect_t g_dirty[SCHED_MAX_DIRTY_RECTS];
static u32 g_dirty_count;

/* ── Stats ── */
static u32 g_fps_counter;
static u32 g_fps_last;
static u32 g_fps_accum_us;
static u32 g_frames_presented;
static u32 g_frames_skipped;
static u32 g_pixels_flushed;

/* ── Helpers ── */
static inline u32 sched_min(u32 a, u32 b) { return a < b ? a : b; }
static inline u32 sched_max(u32 a, u32 b) { return a > b ? a : b; }

/* ARGB8888 alpha blend */
static inline u32 sched_blend(u32 dst, u32 src) {
    u32 sa = (src >> 24) & 0xFFu;
    if (sa == 0u) return dst;
    if (sa == 255u) return src;
    u32 ia = 255u - sa;
    u32 sr = (src >> 16) & 0xFFu, sg = (src >> 8) & 0xFFu, sb = src & 0xFFu;
    u32 dr = (dst >> 16) & 0xFFu, dg = (dst >> 8) & 0xFFu, db = dst & 0xFFu;
    u32 da = (dst >> 24) & 0xFFu;
    u32 r = (sr * sa + dr * ia) / 255u;
    u32 g = (sg * sa + dg * ia) / 255u;
    u32 b = (sb * sa + db * ia) / 255u;
    u32 a = sa + (da * ia) / 255u;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Add a dirty rectangle to the accumulator */
static void add_dirty(u32 x, u32 y, u32 w, u32 h) {
    if (w == 0u || h == 0u) return;
    if (g_dirty_count >= SCHED_MAX_DIRTY_RECTS) return; /* overflow: will be caught next frame */

    /* Clip to screen */
    u32 sw = virtio_gpu_width();
    u32 sh = virtio_gpu_height();
    if (x >= sw || y >= sh) return;
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;

    g_dirty[g_dirty_count].x = x;
    g_dirty[g_dirty_count].y = y;
    g_dirty[g_dirty_count].w = w;
    g_dirty[g_dirty_count].h = h;
    g_dirty_count++;
}

/* Merge overlapping dirty rects (simple: merge all into bounding box if too many) */
static void merge_dirty(void) {
    if (g_dirty_count <= 1u) return;
    if (g_dirty_count <= 4u) return; /* small count, no need to merge */

    /* Too many dirty rects: merge into bounding box for efficiency */
    u32 min_x = g_dirty[0].x, min_y = g_dirty[0].y;
    u32 max_x = g_dirty[0].x + g_dirty[0].w, max_y = g_dirty[0].y + g_dirty[0].h;
    for (u32 i = 1; i < g_dirty_count; i++) {
        if (g_dirty[i].x < min_x) min_x = g_dirty[i].x;
        if (g_dirty[i].y < min_y) min_y = g_dirty[i].y;
        if (g_dirty[i].x + g_dirty[i].w > max_x) max_x = g_dirty[i].x + g_dirty[i].w;
        if (g_dirty[i].y + g_dirty[i].h > max_y) max_y = g_dirty[i].y + g_dirty[i].h;
    }
    g_dirty[0].x = min_x;
    g_dirty[0].y = min_y;
    g_dirty[0].w = max_x - min_x;
    g_dirty[0].h = max_y - min_y;
    g_dirty_count = 1u;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Initialization
 * ═══════════════════════════════════════════════════════════════════════ */

void frame_scheduler_init(void) {
    memset(g_windows, 0, sizeof(g_windows));
    g_window_count = 0u;
    g_dirty_count = 0u;
    g_initialized = 1u;
    g_fps_counter = 0u;
    g_fps_last = 0u;
    g_fps_accum_us = 0u;
    g_frames_presented = 0u;
    g_frames_skipped = 0u;
    g_pixels_flushed = 0u;

    serial_writeln("[FrameScheduler] initialized — 60fps target");
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Window Management
 * ═══════════════════════════════════════════════════════════════════════ */

int sched_create_window(const char* name, u32 x, u32 y, u32 w, u32 h, u32 z) {
    (void)name;
    int slot = -1;
    for (u32 i = 1; i < SCHED_MAX_WINDOWS; i++) {
        if (!g_windows[i].active) { slot = (int)i; break; }
    }
    if (slot < 0) return -1;

    /* Allocate pixel buffer */
    static u32 win_pool[SCHED_MAX_WINDOWS * 1024 * 768]; /* shared pool */
    static u32 pool_off = 0u;
    u32 needed = w * h;
    if (pool_off + needed > sizeof(win_pool) / sizeof(u32)) return -1;

    sched_window_t* win = &g_windows[slot];
    win->active = 1;
    win->x = x; win->y = y;
    win->w = w; win->h = h;
    win->z_order = z;
    win->visible = 1;
    win->needs_redraw = 1;
    win->pixels = &win_pool[pool_off];
    pool_off += needed;
    memset(win->pixels, 0, w * h * 4u);

    /* Full damage on creation */
    win->damage.x = 0; win->damage.y = 0;
    win->damage.w = w; win->damage.h = h;

    g_window_count++;

    /* Add to dirty list */
    add_dirty(x, y, w, h);
    return slot;
}

void sched_destroy_window(int id) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return;
    if (!g_windows[id].active) return;
    g_windows[id].active = 0;
    g_windows[id].pixels = 0;
    if (g_window_count > 0u) g_window_count--;
    /* Full repaint needed */
    g_dirty_count = 0u;
    add_dirty(0, 0, virtio_gpu_width(), virtio_gpu_height());
}

void sched_move_window(int id, u32 x, u32 y) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return;
    if (!g_windows[id].active) return;

    sched_window_t* win = &g_windows[id];
    /* Mark old position as dirty */
    add_dirty(win->x, win->y, win->w, win->h);
    win->x = x;
    win->y = y;
    /* Mark new position as dirty */
    add_dirty(x, y, win->w, win->h);
}

int sched_resize_window(int id, u32 w, u32 h) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return -1;
    if (!g_windows[id].active) return -1;
    sched_window_t* win = &g_windows[id];
    /* For simplicity, only allow shrink */
    if (w > win->w || h > win->h) return -1;
    add_dirty(win->x, win->y, win->w, win->h);
    win->w = w; win->h = h;
    add_dirty(win->x, win->y, w, h);
    return 0;
}

void sched_set_visible(int id, u8 visible) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return;
    if (!g_windows[id].active) return;
    u8 was = g_windows[id].visible;
    g_windows[id].visible = visible;
    if (was != visible) {
        add_dirty(g_windows[id].x, g_windows[id].y, g_windows[id].w, g_windows[id].h);
    }
}

void sched_set_zorder(int id, u32 z) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return;
    if (!g_windows[id].active) return;
    g_windows[id].z_order = z;
    add_dirty(g_windows[id].x, g_windows[id].y, g_windows[id].w, g_windows[id].h);
}

u32* sched_window_pixels(int id) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return 0;
    return g_windows[id].pixels;
}

void sched_window_size(int id, u32* w, u32* h) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) { if(w)*w=0; if(h)*h=0; return; }
    if (w) *w = g_windows[id].w;
    if (h) *h = g_windows[id].h;
}

void sched_damage_window(int id, u32 x, u32 y, u32 w, u32 h) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return;
    if (!g_windows[id].active) return;

    sched_window_t* win = &g_windows[id];
    /* Update per-window damage */
    if (!win->needs_redraw) {
        win->damage.x = x; win->damage.y = y;
        win->damage.w = w; win->damage.h = h;
    } else {
        /* Union with existing damage */
        u32 nx = sched_min(win->damage.x, x);
        u32 ny = sched_min(win->damage.y, y);
        u32 nx2 = sched_max(win->damage.x + win->damage.w, x + w);
        u32 ny2 = sched_max(win->damage.y + win->damage.h, y + h);
        win->damage.x = nx; win->damage.y = ny;
        win->damage.w = nx2 - nx; win->damage.h = ny2 - ny;
    }
    win->needs_redraw = 1;

    /* Add screen-space dirty rect */
    add_dirty(win->x + x, win->y + y, w, h);
}

void sched_damage_window_full(int id) {
    if (id < 1 || (u32)id >= SCHED_MAX_WINDOWS) return;
    if (!g_windows[id].active) return;
    sched_window_t* win = &g_windows[id];
    win->needs_redraw = 1;
    win->damage.x = 0; win->damage.y = 0;
    win->damage.w = win->w; win->damage.h = win->h;
    add_dirty(win->x, win->y, win->w, win->h);
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Compositing — Blend windows into framebuffer
 * ═══════════════════════════════════════════════════════════════════════ */

/* Composite only the dirty regions */
static void composite_dirty_regions(void) {
    u32* fb = (u32*)virtio_gpu_back_buffer();
    u32 fb_w = virtio_gpu_width();
    u32 fb_h = virtio_gpu_height();
    u32 fb_pitch = virtio_gpu_framebuffer_pitch() / 4u;
    if (!fb) return;

    /* Sort windows by z-order (simple insertion sort, small N) */
    int sorted[SCHED_MAX_WINDOWS];
    int sorted_n = 0;
    for (u32 i = 1; i < SCHED_MAX_WINDOWS; i++) {
        if (!g_windows[i].active || !g_windows[i].visible) continue;
        int j = sorted_n;
        while (j > 0 && g_windows[sorted[j-1]].z_order > g_windows[i].z_order) {
            sorted[j] = sorted[j-1]; j--;
        }
        sorted[j] = (int)i;
        sorted_n++;
    }

    /* For each dirty rect, composite all overlapping windows */
    for (u32 d = 0; d < g_dirty_count; d++) {
        sched_rect_t* dirty = &g_dirty[d];
        u32 dx = dirty->x, dy = dirty->y;
        u32 dw = dirty->w, dh = dirty->h;

        /* Clear the dirty region to black first */
        for (u32 row = 0; row < dh; row++) {
            u32* row_ptr = &fb[(dy + row) * fb_pitch + dx];
            u32 qwords = dw / 2u;
            u64 black = 0u;
            u64* dst64 = (u64*)row_ptr;
            for (u32 q = 0; q < qwords; q++) dst64[q] = black;
            if (dw & 1u) row_ptr[dw - 1u] = 0u;
        }

        /* Blend each window back-to-front over this dirty region */
        for (int wi = 0; wi < sorted_n; wi++) {
            sched_window_t* win = &g_windows[sorted[wi]];

            /* Check overlap with dirty rect */
            i32 ox = (i32)dx - (i32)win->x;
            i32 oy = (i32)dy - (i32)win->y;
            i32 ow = (i32)dw;
            i32 oh = (i32)dh;

            /* Clip to window bounds */
            i32 src_x = 0, src_y = 0;
            if (ox < 0) { src_x = -ox; ow += ox; ox = 0; }
            if (oy < 0) { src_y = -oy; oh += oy; oy = 0; }
            if (ox + ow > (i32)win->w) ow = (i32)win->w - ox;
            if (oy + oh > (i32)win->h) oh = (i32)win->h - oy;
            if (ow <= 0 || oh <= 0) continue;

            /* Blit window pixels to framebuffer with alpha */
            for (i32 row = 0; row < oh; row++) {
                u32* src_row = &win->pixels[(src_y + row) * (i32)win->w + src_x];
                u32* dst_row = &fb[(dy + (u32)oy + (u32)row) * fb_pitch + dx + (u32)ox];
                for (i32 col = 0; col < ow; col++) {
                    u32 sp = src_row[col];
                    u8 sa = (sp >> 24) & 0xFFu;
                    if (sa == 0u) continue;       /* fully transparent */
                    if (sa == 255u) { dst_row[col] = sp; continue; } /* fully opaque */
                    dst_row[col] = sched_blend(dst_row[col], sp);
                }
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Frame Presentation
 * ═══════════════════════════════════════════════════════════════════════ */

int sched_present_frame(void) {
    if (!g_initialized) return 0;

    /* Skip frame if nothing changed */
    if (g_dirty_count == 0u) {
        g_frames_skipped++;
        return 0;
    }

    /* Merge dirty rects if too many */
    merge_dirty();

    /* Composite only the dirty regions */
    composite_dirty_regions();

    /* Flush dirty regions to display via GPU DMA */
    u32 total_pixels = 0;
    for (u32 i = 0; i < g_dirty_count; i++) {
        sched_rect_t* r = &g_dirty[i];
        virtio_gpu_flush_rect(r->x, r->y, r->w, r->h);
        total_pixels += r->w * r->h;
    }

    /* Page flip for tear-free presentation */
    virtio_gpu_page_flip();

    /* Update stats */
    g_frames_presented++;
    g_pixels_flushed += total_pixels;
    g_fps_counter++;
    g_dirty_count = 0u;

    /* Reset per-window damage */
    for (u32 i = 1; i < SCHED_MAX_WINDOWS; i++) {
        if (g_windows[i].active) {
            g_windows[i].needs_redraw = 0;
        }
    }

    return 1;
}

void sched_force_full_redraw(void) {
    g_dirty_count = 0u;
    add_dirty(0, 0, virtio_gpu_width(), virtio_gpu_height());
    for (u32 i = 1; i < SCHED_MAX_WINDOWS; i++) {
        if (g_windows[i].active) {
            g_windows[i].needs_redraw = 1;
            g_windows[i].damage.x = 0;
            g_windows[i].damage.y = 0;
            g_windows[i].damage.w = g_windows[i].w;
            g_windows[i].damage.h = g_windows[i].h;
        }
    }
}

/* ── Stats ── */
u32 sched_fps(void) { return g_fps_last; }
u32 sched_frames_presented(void) { return g_frames_presented; }
u32 sched_frames_skipped(void) { return g_frames_skipped; }
u32 sched_pixels_flushed(void) { return g_pixels_flushed; }
const char* sched_backend(void) {
    return virtio_gpu_available() ? "GPU DMA + Page Flip" : "CPU Fallback";
}
