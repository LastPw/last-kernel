/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: ldp_server.h
 * Purpose: Last Display Protocol (LDP) Server Engine & Compositor Interface
 * =============================================================================
 */

#ifndef LDP_SERVER_H
#define LDP_SERVER_H

#include "display/ldp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Client Connection State ── */
typedef struct {
    u8  active;
    u32 client_pid;
    u64 connect_time;
    u32 surface_count;

    /* Lock-Free Command & Event Ring Buffers */
    ldp_ring_buffer_t cmd_ring;   /* Client -> Server */
    ldp_ring_buffer_t evt_ring;   /* Server -> Client */
} ldp_server_client_t;

/* ── Server Global State ── */
typedef struct {
    u8  initialized;
    u8  running;
    u32 active_clients;
    u32 total_surfaces;
    u64 total_frames_composited;
    u64 total_events_dispatched;

    /* Managed Clients & Surfaces */
    ldp_server_client_t clients[LDP_MAX_CLIENTS];
    ldp_surface_desc_t  surfaces[LDP_MAX_SURFACES];
    u8                  surface_active[LDP_MAX_SURFACES];
} ldp_server_t;

/* ── Public Server APIs ── */
int  ldp_server_init(void);
void ldp_server_shutdown(void);
ldp_server_t* ldp_server_get_state(void);

/* Client Lifecycle */
int  ldp_server_connect_client(u32 client_pid);
void ldp_server_disconnect_client(u32 client_id);

/* Surface Management */
int  ldp_server_create_surface(u32 client_id, const char* title, u32 width, u32 height, u32 format, u32 flags);
void ldp_server_destroy_surface(u32 surface_id);
int  ldp_server_attach_buffers(u32 surface_id, u32* buf0, u32* buf1, u32* buf2, u32 stride);
int  ldp_server_swap_buffers(u32 surface_id);
int  ldp_server_damage_rect(u32 surface_id, u32 x, u32 y, u32 w, u32 h);
int  ldp_server_set_glass_effect(u32 surface_id, u8 blur_radius, u8 opacity, u8 vibrancy, u32 tint_color, u32 border_color);
int  ldp_server_set_state(u32 surface_id, u8 state);

/* Compositor Presentation & Buffer Access */
const u32* ldp_server_get_front_buffer(u32 surface_id);
int  ldp_server_present_surface(u32 surface_id);

/* Input & Event Injection (Dispatched to Client Ring) */
int  ldp_server_post_mouse_event(u32 surface_id, i32 x, i32 y, u8 buttons);
int  ldp_server_post_key_event(u32 surface_id, int keycode, u8 is_press);
int  ldp_server_post_configure(u32 surface_id, u32 new_width, u32 new_height);
int  ldp_server_post_close_request(u32 surface_id);

/* Command Processing Loop */
u32  ldp_server_dispatch_commands(void);

/* Lock-Free SPSC Ring Buffer Operations */
int  ldp_ring_push(ldp_ring_buffer_t* rb, const ldp_packet_t* pkt);
int  ldp_ring_pop(ldp_ring_buffer_t* rb, ldp_packet_t* pkt);
u32  ldp_ring_count(const ldp_ring_buffer_t* rb);

#ifdef __cplusplus
}
#endif

#endif /* LDP_SERVER_H */
