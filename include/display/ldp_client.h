/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: ldp_client.h
 * Purpose: Last Display Protocol (LDP) Client SDK & Application Interface
 * =============================================================================
 */

#ifndef LDP_CLIENT_H
#define LDP_CLIENT_H

#include "display/ldp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward Declarations */
typedef struct ldp_client ldp_client_t;
typedef struct ldp_surface ldp_surface_t;

/* Event Callback Signature */
typedef void (*ldp_event_cb)(ldp_surface_t* surface, const ldp_packet_t* evt, void* user_data);

/* ── Client Context Structure ── */
struct ldp_client {
    u32 client_id;
    u32 client_pid;
    u8  connected;
    ldp_ring_buffer_t* cmd_ring; /* Shared with server */
    ldp_ring_buffer_t* evt_ring; /* Shared with server */
    ldp_surface_t* surfaces[LDP_MAX_SURFACES];
};

/* ── Surface Context Structure ── */
struct ldp_surface {
    ldp_client_t* client;
    u32 surface_id;
    u32 width;
    u32 height;
    u32 stride;
    u32 format;
    u32 flags;
    u8  active;
    ldp_surface_desc_t* desc; /* Pointer to shared surface descriptor */

    /* Allocated client memory buffers (for double/triple buffering) */
    u32* local_buffers[3];
};

/* ── Client Connection Lifecycle ── */
ldp_client_t*  ldp_client_connect(u32 client_pid);
void           ldp_client_disconnect(ldp_client_t* client);

/* ── Surface Operations ── */
ldp_surface_t* ldp_surface_create(ldp_client_t* client, const char* title, u32 width, u32 height, u32 format, u32 flags);
void           ldp_surface_destroy(ldp_surface_t* surface);

/* Drawing & Buffer Swapping (Zero-Copy) */
u32*           ldp_surface_get_draw_buffer(ldp_surface_t* surface);
int            ldp_surface_swap_buffers(ldp_surface_t* surface);
int            ldp_surface_damage(ldp_surface_t* surface, u32 x, u32 y, u32 w, u32 h);

/* Visual Cyber-Glass Attributes */
int            ldp_surface_set_glass(ldp_surface_t* surface, u8 blur_radius, u8 opacity, u8 vibrancy, u32 tint_color);

/* Event Polling Loop */
u32            ldp_client_poll_events(ldp_client_t* client, ldp_event_cb callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* LDP_CLIENT_H */
