#ifndef GPU_COMPOSITOR_H
#define GPU_COMPOSITOR_H

#include "types.h"

/* ── GPU Compositor ──
 *
 * Maintains a stack of layers (windows, cursor, desktop) and composites
 * them into the framebuffer. Only dirty regions are re-composited and
 * flushed to the display.
 *
 * Architecture:
 *   Each layer has its own pixel buffer and damage tracker.
 *   The compositor walks layers back-to-front, compositing each into
 *   the backbuffer. Only pixels that changed are updated.
 *   After compositing, the damaged region is flushed to the display.
 *
 * With VirtIO-GPU: the compositor writes to GPU framebuffer memory,
 * then flushes via VirtIO-GPU commands.
 * Without GPU: the compositor writes to s_backbuffer as before.
 */

#define COMP_MAX_LAYERS    64u
#define COMP_MAX_NAME      32u

typedef enum {
    LAYER_TYPE_DESKTOP = 0,   /* Desktop background */
    LAYER_TYPE_WINDOW  = 1,   /* Application window */
    LAYER_TYPE_PANEL   = 2,   /* Taskbar, dock */
    LAYER_TYPE_CURSOR  = 3,   /* Mouse cursor */
    LAYER_TYPE_OVERLAY = 4,   /* Tooltip, popup */
} layer_type_t;

/* Per-layer damage tracker */
typedef struct {
    u32 min_x, min_y;
    u32 max_x, max_y;
    u8  is_damaged;
} layer_damage_t;

/* A compositing layer */
typedef struct {
    u8      active;
    char    name[COMP_MAX_NAME];
    layer_type_t type;
    u32     x, y;          /* Position on screen */
    u32     width, height;  /* Layer dimensions */
    u32     z_order;        /* Higher = in front */
    u8      visible;        /* 0 = hidden */
    u8      needs_composite;/* 1 = layer content changed */
    u32*    pixels;         /* Layer pixel buffer (ARGB8888) */
    layer_damage_t damage;  /* Per-layer damage */
} compositor_layer_t;

/* Initialize the compositor. Call once after gfx_enter_graphics(). */
void gpu_compositor_init(void);

/* Shutdown the compositor, free all layers. */
void gpu_compositor_shutdown(void);

/* ── Layer Management ── */

/* Create a new layer. Returns layer ID (0..COMP_MAX_LAYERS-1), or -1 on error. */
int gpu_compositor_create_layer(const char* name, layer_type_t type,
                                 u32 x, u32 y, u32 width, u32 height,
                                 u32 z_order);

/* Destroy a layer by ID. */
void gpu_compositor_destroy_layer(int layer_id);

/* Move a layer to a new position. */
void gpu_compositor_move_layer(int layer_id, u32 x, u32 y);

/* Resize a layer. Reallocates the pixel buffer. */
int gpu_compositor_resize_layer(int layer_id, u32 new_width, u32 new_height);

/* Show/hide a layer. */
void gpu_compositor_set_visible(int layer_id, u8 visible);

/* Set z-order of a layer. */
void gpu_compositor_set_z_order(int layer_id, u32 z_order);

/* Get a direct pointer to a layer's pixel buffer for writing. */
u32* gpu_compositor_layer_pixels(int layer_id);

/* Get layer dimensions. */
void gpu_compositor_layer_size(int layer_id, u32* width, u32* height);

/* Get layer position. */
void gpu_compositor_layer_pos(int layer_id, u32* x, u32* y);

/* Mark a region of a layer as dirty (content changed). */
void gpu_compositor_layer_damage(int layer_id, u32 x, u32 y, u32 w, u32 h);

/* Mark entire layer as dirty. */
void gpu_compositor_layer_damage_full(int layer_id);

/* ── Compositing ── */

/* Composite all visible layers into the backbuffer and flush to display.
   Only composites regions that have damage on any layer. */
void gpu_compositor_present(void);

/* Force a full recomposite of all layers. */
void gpu_compositor_present_full(void);

/* Get the number of active layers. */
u32 gpu_compositor_layer_count(void);

#endif /* GPU_COMPOSITOR_H */
