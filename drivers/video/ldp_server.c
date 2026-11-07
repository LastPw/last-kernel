/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: ldp_server.c
 * Purpose: Last Display Protocol (LDP) Server Engine & Compositor Interface
 * =============================================================================
 */

#include "display/ldp_server.h"

#ifdef HOST_TEST
#include <string.h>
#else
#include "common/string.h"
#endif

/* ── Global Server Instance ── */
static ldp_server_t s_ldp_server;

/* ── Inline Helpers ── */
static inline u64 ldp_get_tsc(void) {
#if defined(__x86_64__) || defined(_M_X64)
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
#else
    return 1000u;
#endif
}

static inline void ldp_fence(void) {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

/* ── Lock-Free SPSC Ring Buffer Operations ── */

int ldp_ring_push(ldp_ring_buffer_t* rb, const ldp_packet_t* pkt) {
    if (!rb || !pkt) return -1;

    u32 head = rb->head;
    u32 tail = rb->tail;

    /* Check if full: capacity is power of two */
    if ((head - tail) >= rb->capacity) {
        return -1; /* Ring buffer is saturated */
    }

    u32 idx = head & rb->mask;
    rb->entries[idx] = *pkt;
    ldp_fence();

    rb->head = head + 1u;
    return 0;
}

int ldp_ring_pop(ldp_ring_buffer_t* rb, ldp_packet_t* pkt) {
    if (!rb || !pkt) return -1;

    u32 head = rb->head;
    u32 tail = rb->tail;

    /* Check if empty */
    if (head == tail) {
        return -1; /* No packets available */
    }

    u32 idx = tail & rb->mask;
    *pkt = rb->entries[idx];
    ldp_fence();

    rb->tail = tail + 1u;
    return 0;
}

u32 ldp_ring_count(const ldp_ring_buffer_t* rb) {
    if (!rb) return 0u;
    u32 head = rb->head;
    u32 tail = rb->tail;
    return head - tail;
}

static void ldp_ring_init(ldp_ring_buffer_t* rb) {
    if (!rb) return;
    rb->head = 0u;
    rb->tail = 0u;
    rb->capacity = LDP_RING_BUFFER_SIZE;
    rb->mask = LDP_RING_BUFFER_SIZE - 1u;
#ifdef HOST_TEST
    memset(rb->entries, 0, sizeof(rb->entries));
#else
    for (u32 i = 0; i < LDP_RING_BUFFER_SIZE; i++) {
        rb->entries[i].opcode = 0u;
    }
#endif
}

/* ── Public Server Lifecycle ── */

int ldp_server_init(void) {
    if (s_ldp_server.initialized) return 0;

#ifdef HOST_TEST
    memset(&s_ldp_server, 0, sizeof(s_ldp_server));
#else
    for (u32 i = 0; i < LDP_MAX_CLIENTS; i++) {
        s_ldp_server.clients[i].active = 0u;
    }
    for (u32 i = 0; i < LDP_MAX_SURFACES; i++) {
        s_ldp_server.surface_active[i] = 0u;
    }
#endif

    s_ldp_server.initialized = 1u;
    s_ldp_server.running = 1u;
    s_ldp_server.active_clients = 0u;
    s_ldp_server.total_surfaces = 0u;
    s_ldp_server.total_frames_composited = 0u;
    s_ldp_server.total_events_dispatched = 0u;

    return 0;
}

void ldp_server_shutdown(void) {
    for (u32 i = 0; i < LDP_MAX_SURFACES; i++) {
        if (s_ldp_server.surface_active[i]) {
            ldp_server_destroy_surface(i);
        }
    }
    for (u32 i = 0; i < LDP_MAX_CLIENTS; i++) {
        if (s_ldp_server.clients[i].active) {
            ldp_server_disconnect_client(i);
        }
    }
    s_ldp_server.running = 0u;
    s_ldp_server.initialized = 0u;
}

ldp_server_t* ldp_server_get_state(void) {
    if (!s_ldp_server.initialized) ldp_server_init();
    return &s_ldp_server;
}

/* ── Client Lifecycle ── */

int ldp_server_connect_client(u32 client_pid) {
    if (!s_ldp_server.initialized) ldp_server_init();

    for (u32 i = 0; i < LDP_MAX_CLIENTS; i++) {
        if (!s_ldp_server.clients[i].active) {
            ldp_server_client_t* c = &s_ldp_server.clients[i];
            c->active = 1u;
            c->client_pid = client_pid;
            c->connect_time = ldp_get_tsc();
            c->surface_count = 0u;

            ldp_ring_init(&c->cmd_ring);
            ldp_ring_init(&c->evt_ring);

            s_ldp_server.active_clients++;
            return (int)i;
        }
    }
    return -1;
}

void ldp_server_disconnect_client(u32 client_id) {
    if (client_id >= LDP_MAX_CLIENTS) return;
    ldp_server_client_t* c = &s_ldp_server.clients[client_id];
    if (!c->active) return;

    /* Destroy all surfaces owned by this client */
    for (u32 i = 0; i < LDP_MAX_SURFACES; i++) {
        if (s_ldp_server.surface_active[i] && s_ldp_server.surfaces[i].client_id == client_id) {
            ldp_server_destroy_surface(i);
        }
    }

    c->active = 0u;
    c->client_pid = 0u;
    c->surface_count = 0u;
    if (s_ldp_server.active_clients > 0u) {
        s_ldp_server.active_clients--;
    }
}

/* ── Surface Management ── */

int ldp_server_create_surface(u32 client_id, const char* title, u32 width, u32 height, u32 format, u32 flags) {
    if (!s_ldp_server.initialized) ldp_server_init();
    if (client_id >= LDP_MAX_CLIENTS || !s_ldp_server.clients[client_id].active) return -1;

    u32 w = (width > 0 && width <= 3840) ? width : 800u;
    u32 h = (height > 0 && height <= 2160) ? height : 600u;
    u32 fmt = (format >= LDP_FORMAT_ARGB8888 && format <= LDP_FORMAT_FP16_HDR) ? format : LDP_FORMAT_ARGB8888;

    for (u32 i = 0; i < LDP_MAX_SURFACES; i++) {
        if (!s_ldp_server.surface_active[i]) {
            ldp_surface_desc_t* s = &s_ldp_server.surfaces[i];
#ifdef HOST_TEST
            memset(s, 0, sizeof(*s));
#endif
            s->surface_id = i;
            s->client_id = client_id;
            s->width = w;
            s->height = h;
            s->stride = w * 4u;
            s->format = fmt;
            s->flags = flags;
            s->state = LDP_STATE_NORMAL;
            s->buffer_mode = LDP_BUFFER_TRIPLE;

            /* Set up Triple Buffering indices */
            s->active_front  = 0u;
            s->active_back   = 1u;
            s->active_render = 2u;
            s->buffer_size = s->stride * s->height;

            s->buffers[0] = 0;
            s->buffers[1] = 0;
            s->buffers[2] = 0;

            /* Default Cyber-Glass attributes */
            if (flags & LDP_SURFACE_GLASS_BLUR) {
                s->glass.enabled = 1u;
                s->glass.blur_radius = 16u;
                s->glass.opacity = 220u;
                s->glass.vibrancy = 75u;
                s->glass.tint_color = 0x20152538u;
                s->glass.border_color = 0x4038BDF8u;
            } else {
                s->glass.enabled = 0u;
                s->glass.blur_radius = 0u;
                s->glass.opacity = 255u;
                s->glass.vibrancy = 0u;
                s->glass.tint_color = 0u;
                s->glass.border_color = 0u;
            }

            s->damage_count = 0u;
            s->frame_sequence = 0u;
            s->last_present_time = ldp_get_tsc();

            if (title) {
#ifdef HOST_TEST
                strncpy(s->title, title, LDP_MAX_TITLE_LEN - 1);
#else
                str_ncpy(s->title, title, LDP_MAX_TITLE_LEN - 1);
#endif
                s->title[LDP_MAX_TITLE_LEN - 1] = '\0';
            } else {
#ifdef HOST_TEST
                strcpy(s->title, "LDP Window");
#else
                str_cpy(s->title, "LDP Window");
#endif
            }

            s_ldp_server.surface_active[i] = 1u;
            s_ldp_server.clients[client_id].surface_count++;
            s_ldp_server.total_surfaces++;

            /* Post creation event to client event ring */
            ldp_packet_t evt;
            evt.opcode = LDP_EVT_SURFACE_CREATED;
            evt.surface_id = (u16)i;
            evt.client_id = client_id;
            evt.param1 = w;
            evt.param2 = h;
            evt.param3 = s->stride;
            evt.param4 = fmt;
            evt.timestamp = ldp_get_tsc();
            ldp_ring_push(&s_ldp_server.clients[client_id].evt_ring, &evt);

            return (int)i;
        }
    }
    return -1;
}

void ldp_server_destroy_surface(u32 surface_id) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    u32 cid = s->client_id;
    if (cid < LDP_MAX_CLIENTS && s_ldp_server.clients[cid].active) {
        if (s_ldp_server.clients[cid].surface_count > 0u) {
            s_ldp_server.clients[cid].surface_count--;
        }
    }

    s_ldp_server.surface_active[surface_id] = 0u;
    if (s_ldp_server.total_surfaces > 0u) {
        s_ldp_server.total_surfaces--;
    }
}

int ldp_server_attach_buffers(u32 surface_id, u32* buf0, u32* buf1, u32* buf2, u32 stride) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;
    if (!buf0) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    s->buffers[0] = buf0;
    s->buffers[1] = buf1 ? buf1 : buf0;
    s->buffers[2] = buf2 ? buf2 : (buf1 ? buf1 : buf0);
    if (stride > 0u) s->stride = stride;

    return 0;
}

int ldp_server_swap_buffers(u32 surface_id) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];

    /* Zero-Wait Lock-Free Triple Buffering Rotation:
     * 1. The client finished drawing into `active_render`.
     * 2. `active_render` becomes `active_back` (ready for compositor).
     * 3. A spare slot is recycled into `active_render` for uninterrupted client drawing.
     */
    u8 old_render = s->active_render;
    u8 old_front  = s->active_front;

    /* Rotate: back gets newly rendered buffer */
    s->active_back = old_render;

    /* Render buffer gets the free slot that is neither front nor new back */
    u8 free_slot = 0u;
    for (u8 slot = 0u; slot < 3u; slot++) {
        if (slot != old_front && slot != old_render) {
            free_slot = slot;
            break;
        }
    }
    s->active_render = free_slot;

    s->frame_sequence++;
    ldp_fence();

    /* Send VSync presentation tick back to client */
    u32 cid = s->client_id;
    if (cid < LDP_MAX_CLIENTS && s_ldp_server.clients[cid].active) {
        ldp_packet_t evt;
        evt.opcode = LDP_EVT_VSYNC_FRAME;
        evt.surface_id = (u16)surface_id;
        evt.client_id = cid;
        evt.param1 = (u32)(s->frame_sequence & 0xFFFFFFFFu);
        evt.param2 = 0u;
        evt.param3 = 0u;
        evt.param4 = 0u;
        evt.timestamp = ldp_get_tsc();
        ldp_ring_push(&s_ldp_server.clients[cid].evt_ring, &evt);
    }

    return 0;
}

int ldp_server_damage_rect(u32 surface_id, u32 x, u32 y, u32 w, u32 h) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    if (s->damage_count < LDP_MAX_DAMAGE_RECTS) {
        s->damages[s->damage_count].x = x;
        s->damages[s->damage_count].y = y;
        s->damages[s->damage_count].width = w;
        s->damages[s->damage_count].height = h;
        s->damage_count++;
    } else {
        /* Merge into whole surface damage */
        s->damages[0].x = 0;
        s->damages[0].y = 0;
        s->damages[0].width = s->width;
        s->damages[0].height = s->height;
        s->damage_count = 1u;
    }
    return 0;
}

int ldp_server_set_glass_effect(u32 surface_id, u8 blur_radius, u8 opacity, u8 vibrancy, u32 tint_color, u32 border_color) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    s->glass.enabled = (blur_radius > 0u || opacity < 255u) ? 1u : 0u;
    s->glass.blur_radius = blur_radius > 32u ? 32u : blur_radius;
    s->glass.opacity = opacity;
    s->glass.vibrancy = vibrancy > 100u ? 100u : vibrancy;
    s->glass.tint_color = tint_color;
    s->glass.border_color = border_color;
    return 0;
}

int ldp_server_set_state(u32 surface_id, u8 state) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;
    s_ldp_server.surfaces[surface_id].state = state;
    return 0;
}

/* ── Compositor Presentation & Buffer Access ── */

const u32* ldp_server_get_front_buffer(u32 surface_id) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return 0;
    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    return s->buffers[s->active_front];
}

int ldp_server_present_surface(u32 surface_id) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    /* Promote back buffer to front buffer */
    s->active_front = s->active_back;
    s->damage_count = 0u;
    s->last_present_time = ldp_get_tsc();
    s_ldp_server.total_frames_composited++;
    ldp_fence();

    return 0;
}

/* ── Input & Event Injection ── */

int ldp_server_post_mouse_event(u32 surface_id, i32 x, i32 y, u8 buttons) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    u32 cid = s->client_id;
    if (cid >= LDP_MAX_CLIENTS || !s_ldp_server.clients[cid].active) return -1;

    ldp_packet_t pkt;
    pkt.opcode = buttons ? LDP_EVT_MOUSE_BUTTON : LDP_EVT_MOUSE_MOVE;
    pkt.surface_id = (u16)surface_id;
    pkt.client_id = cid;
    pkt.param1 = (u32)x;
    pkt.param2 = (u32)y;
    pkt.param3 = (u32)buttons;
    pkt.param4 = 0u;
    pkt.timestamp = ldp_get_tsc();

    int res = ldp_ring_push(&s_ldp_server.clients[cid].evt_ring, &pkt);
    if (res == 0) s_ldp_server.total_events_dispatched++;
    return res;
}

int ldp_server_post_key_event(u32 surface_id, int keycode, u8 is_press) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    u32 cid = s->client_id;
    if (cid >= LDP_MAX_CLIENTS || !s_ldp_server.clients[cid].active) return -1;

    ldp_packet_t pkt;
    pkt.opcode = is_press ? LDP_EVT_KEY_PRESS : LDP_EVT_KEY_RELEASE;
    pkt.surface_id = (u16)surface_id;
    pkt.client_id = cid;
    pkt.param1 = (u32)keycode;
    pkt.param2 = (u32)is_press;
    pkt.param3 = 0u;
    pkt.param4 = 0u;
    pkt.timestamp = ldp_get_tsc();

    int res = ldp_ring_push(&s_ldp_server.clients[cid].evt_ring, &pkt);
    if (res == 0) s_ldp_server.total_events_dispatched++;
    return res;
}

int ldp_server_post_configure(u32 surface_id, u32 new_width, u32 new_height) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    u32 cid = s->client_id;
    if (cid >= LDP_MAX_CLIENTS || !s_ldp_server.clients[cid].active) return -1;

    s->width = new_width;
    s->height = new_height;
    s->stride = new_width * 4u;
    s->buffer_size = s->stride * s->height;

    ldp_packet_t pkt;
    pkt.opcode = LDP_EVT_SURFACE_CONFIG;
    pkt.surface_id = (u16)surface_id;
    pkt.client_id = cid;
    pkt.param1 = new_width;
    pkt.param2 = new_height;
    pkt.param3 = s->stride;
    pkt.param4 = s->format;
    pkt.timestamp = ldp_get_tsc();

    int res = ldp_ring_push(&s_ldp_server.clients[cid].evt_ring, &pkt);
    if (res == 0) s_ldp_server.total_events_dispatched++;
    return res;
}

int ldp_server_post_close_request(u32 surface_id) {
    if (surface_id >= LDP_MAX_SURFACES || !s_ldp_server.surface_active[surface_id]) return -1;

    ldp_surface_desc_t* s = &s_ldp_server.surfaces[surface_id];
    u32 cid = s->client_id;
    if (cid >= LDP_MAX_CLIENTS || !s_ldp_server.clients[cid].active) return -1;

    ldp_packet_t pkt;
    pkt.opcode = LDP_EVT_CLOSE_REQUEST;
    pkt.surface_id = (u16)surface_id;
    pkt.client_id = cid;
    pkt.param1 = 0u;
    pkt.param2 = 0u;
    pkt.param3 = 0u;
    pkt.param4 = 0u;
    pkt.timestamp = ldp_get_tsc();

    int res = ldp_ring_push(&s_ldp_server.clients[cid].evt_ring, &pkt);
    if (res == 0) s_ldp_server.total_events_dispatched++;
    return res;
}

/* ── Command Processing Loop ── */

u32 ldp_server_dispatch_commands(void) {
    if (!s_ldp_server.initialized || !s_ldp_server.running) return 0u;

    u32 processed = 0u;
    for (u32 cid = 0; cid < LDP_MAX_CLIENTS; cid++) {
        if (!s_ldp_server.clients[cid].active) continue;

        ldp_ring_buffer_t* ring = &s_ldp_server.clients[cid].cmd_ring;
        ldp_packet_t pkt;

        while (ldp_ring_pop(ring, &pkt) == 0) {
            processed++;
            switch (pkt.opcode) {
                case LDP_CMD_DISCONNECT:
                    ldp_server_disconnect_client(cid);
                    break;

                case LDP_CMD_CREATE_SURFACE:
                    (void)ldp_server_create_surface(cid, "Client Surface", pkt.param1, pkt.param2, pkt.param3, pkt.param4);
                    break;

                case LDP_CMD_DESTROY_SURFACE:
                    ldp_server_destroy_surface(pkt.surface_id);
                    break;

                case LDP_CMD_SWAP_BUFFERS:
                    (void)ldp_server_swap_buffers(pkt.surface_id);
                    break;

                case LDP_CMD_DAMAGE_RECT:
                    (void)ldp_server_damage_rect(pkt.surface_id, pkt.param1, pkt.param2, pkt.param3, pkt.param4);
                    break;

                case LDP_CMD_SET_STATE:
                    (void)ldp_server_set_state(pkt.surface_id, (u8)pkt.param1);
                    break;

                case LDP_CMD_SET_GLASS_EFFECT:
                    (void)ldp_server_set_glass_effect(pkt.surface_id, (u8)pkt.param1, (u8)pkt.param2, (u8)pkt.param3, pkt.param4, 0x4038BDF8u);
                    break;

                default:
                    break;
            }
        }
    }
    return processed;
}
