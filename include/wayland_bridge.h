#ifndef WAYLAND_BRIDGE_H
#define WAYLAND_BRIDGE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAYLAND_MAX_CLIENTS 8
#define WAYLAND_MAX_SURFACES 16

/* Wayland Buffer Formats */
#define WL_SHM_FORMAT_ARGB8888 0
#define WL_SHM_FORMAT_XRGB8888 1
#define WL_SHM_FORMAT_RGBA8888 2

/* Wayland Surface State */
typedef struct {
    u8 active;
    u32 surface_id;
    u32 client_id;
    char title[64];
    u32 width;
    u32 height;
    u32 stride;
    u32 format;
    const u32* pixel_buffer;
    u8 damage_pending;
    u32 app_window_id; /* Tied to LastOS window instance */
} wayland_surface_t;

/* Wayland Bridge Server State */
typedef struct {
    u8 server_running;
    u32 active_clients;
    u32 total_frames_composited;
    wayland_surface_t surfaces[WAYLAND_MAX_SURFACES];
} wayland_server_t;

/* ── Modular Wayland Bridge APIs (Userland / Subsystem Level) ── */
int wayland_server_init(void);
void wayland_server_shutdown(void);
wayland_server_t* wayland_server_get_state(void);

/* Client Connection & Surface Management */
int wayland_create_surface(u32 client_id, const char* title, u32 width, u32 height);
int wayland_attach_shm_buffer(u32 surface_id, const u32* pixels, u32 stride, u32 format);
int wayland_commit_surface(u32 surface_id);
void wayland_destroy_surface(u32 surface_id);

/* Input Event Dispatch from Desktop Compositor to Wayland Client */
void wayland_dispatch_mouse(u32 surface_id, i32 rel_x, i32 rel_y, u8 buttons);
void wayland_dispatch_key(u32 surface_id, int key, u8 is_press);

/* Render Callback */
void wayland_surface_render_to_window(u32 surface_idx, u32 body_x, u32 body_y, u32 body_w, u32 body_h);

#ifdef __cplusplus
}
#endif

#endif /* WAYLAND_BRIDGE_H */
