#include "display_mst.h"
#include "serial.h"
#include "string.h"

static display_mst_topology_t g_display_topo;
static u8 g_mst_initialized = 0;

int display_mst_init(void) {
    memset(&g_display_topo, 0, sizeof(g_display_topo));
    g_display_topo.monitor_count = 2;

    // Monitor 1: Main 4K UltraHD Monitor
    g_display_topo.monitors[0].display_id = 1;
    str_cpy(g_display_topo.monitors[0].name, "Dell UltraSharp 4K (DP-1)");
    g_display_topo.monitors[0].width = 3840;
    g_display_topo.monitors[0].height = 2160;
    g_display_topo.monitors[0].refresh_rate_hz = 60;
    g_display_topo.monitors[0].offset_x = 0;
    g_display_topo.monitors[0].offset_y = 0;
    g_display_topo.monitors[0].is_primary = 1;
    g_display_topo.monitors[0].active = 1;

    // Monitor 2: Secondary 1080p Vertical Monitor
    g_display_topo.monitors[1].display_id = 2;
    str_cpy(g_display_topo.monitors[1].name, "LG IPS Gaming HD (DP-2 MST)");
    g_display_topo.monitors[1].width = 1920;
    g_display_topo.monitors[1].height = 1080;
    g_display_topo.monitors[1].refresh_rate_hz = 60;
    g_display_topo.monitors[1].offset_x = 3840;
    g_display_topo.monitors[1].offset_y = 0;
    g_display_topo.monitors[1].is_primary = 0;
    g_display_topo.monitors[1].active = 1;

    g_display_topo.total_desktop_w = 5760;
    g_display_topo.total_desktop_h = 2160;
    g_mst_initialized = 1;

    serial_writeln("[DISPLAY-MST] DisplayPort MST Multi-Monitor Topology Initialized (Dual Monitor Extended Desktop 5760x2160)");
    return 0;
}

int display_mst_get_topology(display_mst_topology_t* out_topo) {
    if (!g_mst_initialized || !out_topo) return -1;
    *out_topo = g_display_topo;
    return 0;
}

int display_mst_set_primary(u32 display_id) {
    if (!g_mst_initialized) return -1;
    for (u32 i = 0; i < g_display_topo.monitor_count; i++) {
        g_display_topo.monitors[i].is_primary = (g_display_topo.monitors[i].display_id == display_id) ? 1 : 0;
    }
    return 0;
}
