/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: ldp_client.c
 * Purpose: Last Display Protocol (LDP) Client SDK Library Implementation
 * =============================================================================
 */

#include "display/ldp_client.h"
#include "display/ldp_server.h"

#ifdef HOST_TEST
#include <stdlib.h>
#include <string.h>
#define ldp_malloc malloc
#define ldp_free   free
#else
#include "common/string.h"
#include "mm/heap.h"
#define ldp_malloc kmalloc
#define ldp_free   kfree
#endif

#define MAX_ACTIVE_CLIENT_CONTEXTS 16
static ldp_client_t  s_client_contexts[MAX_ACTIVE_CLIENT_CONTEXTS];
static ldp_surface_t s_surface_contexts[LDP_MAX_SURFACES];
static u8 s_client_slots_used[MAX_ACTIVE_CLIENT_CONTEXTS];
static u8 s_surface_slots_used[LDP_MAX_SURFACES];

ldp_client_t* ldp_client_connect(u32 client_pid) {
    int server_cid = ldp_server_connect_client(client_pid);
    if (server_cid < 0) return 0;

    int slot = -1;
    for (int i = 0; i < MAX_ACTIVE_CLIENT_CONTEXTS; i++) {
        if (!s_client_slots_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        ldp_server_disconnect_client((u32)server_cid);
        return 0;
    }

    ldp_client_t* client = &s_client_contexts[slot];
#ifdef HOST_TEST
    memset(client, 0, sizeof(*client));
#endif
    s_client_slots_used[slot] = 1u;

    client->client_id = (u32)server_cid;
    client->client_pid = client_pid;
    client->connected = 1u;

    ldp_server_t* srv = ldp_server_get_state();
    client->cmd_ring = &srv->clients[server_cid].cmd_ring;
    client->evt_ring = &srv->clients[server_cid].evt_ring;

    for (u32 i = 0; i < LDP_MAX_SURFACES; i++) {
        client->surfaces[i] = 0;
    }

    return client;
}

void ldp_client_disconnect(ldp_client_t* client) {
    if (!client || !client->connected) return;

    /* Destroy all client-side surfaces */
    for (u32 i = 0; i < LDP_MAX_SURFACES; i++) {
        if (client->surfaces[i]) {
            ldp_surface_destroy(client->surfaces[i]);
            client->surfaces[i] = 0;
        }
    }

    ldp_packet_t cmd;
    cmd.opcode = LDP_CMD_DISCONNECT;
    cmd.surface_id = 0u;
    cmd.client_id = client->client_id;
    cmd.param1 = 0u;
    cmd.param2 = 0u;
    cmd.param3 = 0u;
    cmd.param4 = 0u;
    cmd.timestamp = 0u;
    ldp_ring_push(client->cmd_ring, &cmd);

    ldp_server_disconnect_client(client->client_id);
    client->connected = 0u;

    for (int i = 0; i < MAX_ACTIVE_CLIENT_CONTEXTS; i++) {
        if (&s_client_contexts[i] == client) {
            s_client_slots_used[i] = 0u;
            break;
        }
    }
}

ldp_surface_t* ldp_surface_create(ldp_client_t* client, const char* title, u32 width, u32 height, u32 format, u32 flags) {
    if (!client || !client->connected) return 0;

    int sid = ldp_server_create_surface(client->client_id, title, width, height, format, flags);
    if (sid < 0) return 0;

    int surf_slot = -1;
    for (int i = 0; i < (int)LDP_MAX_SURFACES; i++) {
        if (!s_surface_slots_used[i]) {
            surf_slot = i;
            break;
        }
    }
    if (surf_slot < 0) {
        ldp_server_destroy_surface((u32)sid);
        return 0;
    }

    ldp_surface_t* surface = &s_surface_contexts[surf_slot];
#ifdef HOST_TEST
    memset(surface, 0, sizeof(*surface));
#endif
    s_surface_slots_used[surf_slot] = 1u;

    surface->client = client;
    surface->surface_id = (u32)sid;
    surface->width = (width > 0 && width <= 3840) ? width : 800u;
    surface->height = (height > 0 && height <= 2160) ? height : 600u;
    surface->stride = surface->width * 4u;
    surface->format = format;
    surface->flags = flags;
    surface->active = 1u;

    ldp_server_t* srv = ldp_server_get_state();
    surface->desc = &srv->surfaces[sid];

    /* Allocate Triple Buffers (ARGB32) */
    u32 total_pixels = surface->width * surface->height;
    for (int b = 0; b < 3; b++) {
        surface->local_buffers[b] = (u32*)ldp_malloc(total_pixels * sizeof(u32));
        if (surface->local_buffers[b]) {
#ifdef HOST_TEST
            memset(surface->local_buffers[b], 0, total_pixels * sizeof(u32));
#endif
        }
    }

    /* Attach buffers to server */
    ldp_server_attach_buffers((u32)sid, surface->local_buffers[0], surface->local_buffers[1], surface->local_buffers[2], surface->stride);

    client->surfaces[sid] = surface;
    return surface;
}

void ldp_surface_destroy(ldp_surface_t* surface) {
    if (!surface || !surface->active) return;

    u32 sid = surface->surface_id;
    if (surface->client) {
        surface->client->surfaces[sid] = 0;
    }

    /* Free triple buffers */
    for (int b = 0; b < 3; b++) {
        if (surface->local_buffers[b]) {
            ldp_free(surface->local_buffers[b]);
            surface->local_buffers[b] = 0;
        }
    }

    ldp_server_destroy_surface(sid);
    surface->active = 0u;

    for (int i = 0; i < (int)LDP_MAX_SURFACES; i++) {
        if (&s_surface_contexts[i] == surface) {
            s_surface_slots_used[i] = 0u;
            break;
        }
    }
}

u32* ldp_surface_get_draw_buffer(ldp_surface_t* surface) {
    if (!surface || !surface->active || !surface->desc) return 0;
    u8 render_idx = surface->desc->active_render;
    if (render_idx > 2u) render_idx = 0u;
    return surface->local_buffers[render_idx];
}

int ldp_surface_swap_buffers(ldp_surface_t* surface) {
    if (!surface || !surface->active || !surface->client) return -1;

    /* Push Swap Command to SPSC Command Ring */
    ldp_packet_t cmd;
    cmd.opcode = LDP_CMD_SWAP_BUFFERS;
    cmd.surface_id = (u16)surface->surface_id;
    cmd.client_id = surface->client->client_id;
    cmd.param1 = 0u;
    cmd.param2 = 0u;
    cmd.param3 = 0u;
    cmd.param4 = 0u;
    cmd.timestamp = 0u;

    int ret = ldp_ring_push(surface->client->cmd_ring, &cmd);
    if (ret == 0) {
        /* Directly dispatch in server */
        (void)ldp_server_swap_buffers(surface->surface_id);
    }
    return ret;
}

int ldp_surface_damage(ldp_surface_t* surface, u32 x, u32 y, u32 w, u32 h) {
    if (!surface || !surface->active || !surface->client) return -1;

    ldp_packet_t cmd;
    cmd.opcode = LDP_CMD_DAMAGE_RECT;
    cmd.surface_id = (u16)surface->surface_id;
    cmd.client_id = surface->client->client_id;
    cmd.param1 = x;
    cmd.param2 = y;
    cmd.param3 = w;
    cmd.param4 = h;
    cmd.timestamp = 0u;

    int ret = ldp_ring_push(surface->client->cmd_ring, &cmd);
    if (ret == 0) {
        (void)ldp_server_damage_rect(surface->surface_id, x, y, w, h);
    }
    return ret;
}

int ldp_surface_set_glass(ldp_surface_t* surface, u8 blur_radius, u8 opacity, u8 vibrancy, u32 tint_color) {
    if (!surface || !surface->active || !surface->client) return -1;

    ldp_packet_t cmd;
    cmd.opcode = LDP_CMD_SET_GLASS_EFFECT;
    cmd.surface_id = (u16)surface->surface_id;
    cmd.client_id = surface->client->client_id;
    cmd.param1 = (u32)blur_radius;
    cmd.param2 = (u32)opacity;
    cmd.param3 = (u32)vibrancy;
    cmd.param4 = tint_color;
    cmd.timestamp = 0u;

    int ret = ldp_ring_push(surface->client->cmd_ring, &cmd);
    if (ret == 0) {
        (void)ldp_server_set_glass_effect(surface->surface_id, blur_radius, opacity, vibrancy, tint_color, 0x4038BDF8u);
    }
    return ret;
}

u32 ldp_client_poll_events(ldp_client_t* client, ldp_event_cb callback, void* user_data) {
    if (!client || !client->connected || !client->evt_ring) return 0u;

    u32 count = 0u;
    ldp_packet_t evt;

    while (ldp_ring_pop(client->evt_ring, &evt) == 0) {
        count++;
        u16 sid = evt.surface_id;
        ldp_surface_t* surf = (sid < LDP_MAX_SURFACES) ? client->surfaces[sid] : 0;
        if (callback) {
            callback(surf, &evt, user_data);
        }
    }
    return count;
}
