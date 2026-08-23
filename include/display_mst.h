#ifndef DISPLAY_MST_H
#define DISPLAY_MST_H

#include "types.h"

typedef struct {
    u32 display_id;
    char name[32];
    u32 width;
    u32 height;
    u32 refresh_rate_hz;
    i32 offset_x;
    i32 offset_y;
    u8 is_primary;
    u8 active;
} display_monitor_t;

typedef struct {
    u32 monitor_count;
    display_monitor_t monitors[4];
    u32 total_desktop_w;
    u32 total_desktop_h;
} display_mst_topology_t;

int display_mst_init(void);
int display_mst_get_topology(display_mst_topology_t* out_topo);
int display_mst_set_primary(u32 display_id);

#endif
