#ifndef FRAME_SCHEDULER_H
#define FRAME_SCHEDULER_H

#include "types.h"

/* ═══════════════════════════════════════════════════════════════════════
 *  Frame Scheduler — 60fps VSync-Aware Presentation
 *
 *  Orchestrates the entire rendering pipeline:
 *    1. Collect dirty regions from all windows
 *    2. Composite only changed regions (not full screen)
 *    3. Flush via GPU DMA (not CPU memcpy)
 *    4. Page flip at vsync (no tearing)
 *
 *  Result: smooth 60fps desktop with minimal GPU/CPU work.
 * ═══════════════════════════════════════════════════════════════════════ */

/* Maximum dirty rectangles tracked per frame */
#define SCHED_MAX_DIRTY_RECTS  128u

/* A dirty rectangle: a region of the screen that needs repainting */
typedef struct {
    u32 x, y, w, h;
} sched_rect_t;

/* Per-window state tracked by the scheduler */
typedef struct {
    u8    active;
    u32   x, y, w, h;       /* window position and size */
    u32   z_order;
    u8    visible;
    u8    needs_redraw;      /* window content changed */
    sched_rect_t damage;     /* damaged region within window */
    u32*  pixels;            /* window pixel buffer */
} sched_window_t;

#define SCHED_MAX_WINDOWS  64u

/* ── Initialize the frame scheduler ──
   Call once during boot, after gfx_enter_graphics() and gpu_engine_init(). */
void frame_scheduler_init(void);

/* ── Window Management ── */

/* Register a window. Returns window ID (1..SCHED_MAX_WINDOWS-1). */
int sched_create_window(const char* name, u32 x, u32 y, u32 w, u32 h, u32 z);

/* Destroy a window. */
void sched_destroy_window(int id);

/* Move a window. */
void sched_move_window(int id, u32 x, u32 y);

/* Resize a window. */
int sched_resize_window(int id, u32 w, u32 h);

/* Show/hide a window. */
void sched_set_visible(int id, u8 visible);

/* Set z-order. */
void sched_set_zorder(int id, u32 z);

/* Get direct access to window pixel buffer. */
u32* sched_window_pixels(int id);

/* Get window dimensions. */
void sched_window_size(int id, u32* w, u32* h);

/* Mark a region of a window as dirty (content changed). */
void sched_damage_window(int id, u32 x, u32 y, u32 w, u32 h);

/* Mark entire window as dirty. */
void sched_damage_window_full(int id);

/* ── Frame Presentation ── */

/* Present one frame: composite dirty regions, flush to display, page flip.
   Call this once per frame in the main loop.
   Returns 1 if a frame was actually presented, 0 if nothing changed. */
int sched_present_frame(void);

/* Force a full repaint of everything (e.g., after window resize). */
void sched_force_full_redraw(void);

/* ── Stats ── */
u32  sched_fps(void);            /* current FPS */
u32  sched_frames_presented(void); /* total frames presented */
u32  sched_frames_skipped(void);   /* frames skipped (no dirty regions) */
u32  sched_pixels_flushed(void);   /* total pixels flushed to display */
const char* sched_backend(void);   /* "GPU DMA" or "CPU" */

#endif /* FRAME_SCHEDULER_H */
