#ifndef LASTOS_SPATIAL_CANVAS_H
#define LASTOS_SPATIAL_CANVAS_H

#include "types.h"
#include "desktop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zoom is stored in 8.8 fixed-point (256 = 100%, 128 = 50%, 512 = 200%) */
#define SPATIAL_ZOOM_100  256
#define SPATIAL_ZOOM_MIN  90   /* ~35% overview */
#define SPATIAL_ZOOM_MAX  512  /* 200% zoom-in */

typedef enum {
    CLUSTER_DEV_STUDIO = 0,
    CLUSTER_CYBER_ARCADE = 1,
    CLUSTER_SOVEREIGN_WEB = 2,
    CLUSTER_SECURITY_OPS = 3,
    CLUSTER_COUNT = 4
} spatial_cluster_id_t;

typedef struct {
    const char* name;
    const char* subtitle;
    i32 world_x;
    i32 world_y;
    u32 world_w;
    u32 world_h;
    u32 accent_color;
    app_id_t app_members[3];
    u32 member_count;
} spatial_cluster_t;

typedef struct {
    /* Current Camera State */
    i32 cam_x;
    i32 cam_y;
    i32 cam_zoom; /* 8.8 fixed point */

    /* Target Camera State (Smooth Interpolation) */
    i32 target_x;
    i32 target_y;
    i32 target_zoom;

    /* Mode */
    u8 is_overview_mode;
    u8 is_panning;
    i32 pan_start_mx;
    i32 pan_start_my;
    i32 pan_start_cam_x;
    i32 pan_start_cam_y;

    /* Clusters */
    spatial_cluster_t clusters[CLUSTER_COUNT];
    spatial_cluster_id_t active_cluster;

    /* Animation tick */
    u32 anim_tick;
} spatial_canvas_t;

void spatial_canvas_init(void);
void spatial_canvas_update(void);
spatial_canvas_t* spatial_canvas_get(void);

/* Coordinate Projectors */
i32 spatial_world_to_screen_x(i32 world_x);
i32 spatial_world_to_screen_y(i32 world_y);
i32 spatial_screen_to_world_x(i32 screen_x);
i32 spatial_screen_to_world_y(i32 screen_y);
u32 spatial_scale_dim(u32 dim);

/* Navigation & Gestures */
void spatial_canvas_toggle_overview(void);
void spatial_canvas_fly_to_cluster(spatial_cluster_id_t cluster_id);
void spatial_canvas_zoom(int delta);
void spatial_canvas_pan(int dx, int dy);
void spatial_canvas_render_overview_hud(void);
int  spatial_canvas_handle_click(i32 mx, i32 my);

#ifdef __cplusplus
}
#endif

#endif /* LASTOS_SPATIAL_CANVAS_H */
