/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: ldp_protocol.h
 * Purpose: Last Display Protocol (LDP) Core Specification & Wire Definitions
 * =============================================================================
 */

#ifndef LDP_PROTOCOL_H
#define LDP_PROTOCOL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Magic & Version Identifiers ── */
#define LDP_MAGIC                0x4C445031u /* 'LDP1' (Last Display Protocol v1) */
#define LDP_VERSION_MAJOR        1u
#define LDP_VERSION_MINOR        0u

/* ── Limits & Capacities ── */
#define LDP_MAX_CLIENTS          32u
#define LDP_MAX_SURFACES         64u
#define LDP_RING_BUFFER_SIZE     128u /* Must be a power of 2 for fast bitmasking */
#define LDP_MAX_TITLE_LEN        64u
#define LDP_MAX_DAMAGE_RECTS     16u

/* ── Pixel Formats ── */
#define LDP_FORMAT_ARGB8888      0x01u /* 32-bit ARGB (8:8:8:8, Standard Cyber-Glass) */
#define LDP_FORMAT_XRGB8888      0x02u /* 32-bit XRGB (8:8:8:8, Opaque) */
#define LDP_FORMAT_RGB565        0x03u /* 16-bit RGB (5:6:5, High-Speed Low-Memory) */
#define LDP_FORMAT_RGBA8888      0x04u /* 32-bit RGBA (8:8:8:8) */
#define LDP_FORMAT_FP16_HDR      0x05u /* 64-bit Half-Float HDR */

/* ── Buffer Modes ── */
#define LDP_BUFFER_SINGLE        0x01u /* Direct single buffer */
#define LDP_BUFFER_DOUBLE        0x02u /* Ping-pong double buffering */
#define LDP_BUFFER_TRIPLE        0x03u /* Zero-latency Lock-free Triple Buffering */

/* ── Surface Flags & Capabilities ── */
#define LDP_SURFACE_TOPLEVEL     0x0001u /* Standard application window */
#define LDP_SURFACE_POPUP        0x0002u /* Context menu, dropdown, or tooltip */
#define LDP_SURFACE_FULLSCREEN   0x0004u /* Fullscreen dedicated surface */
#define LDP_SURFACE_TRANSPARENT  0x0008u /* Has alpha-channel transparency */
#define LDP_SURFACE_GLASS_BLUR   0x0010u /* Enable GPU background cyber-glass blur */
#define LDP_SURFACE_DECORATED    0x0020u /* Server-side titlebar & control buttons */
#define LDP_SURFACE_NO_FOCUS     0x0040u /* Does not steal keyboard focus */

/* ── Window States ── */
#define LDP_STATE_NORMAL         0u
#define LDP_STATE_MINIMIZED      1u
#define LDP_STATE_MAXIMIZED      2u
#define LDP_STATE_SNAPPED_LEFT   3u
#define LDP_STATE_SNAPPED_RIGHT  4u
#define LDP_STATE_FULLSCREEN     5u

/* ── Client Command Opcodes (Client -> Compositor) ── */
#define LDP_CMD_CONNECT          0x0001u
#define LDP_CMD_DISCONNECT       0x0002u
#define LDP_CMD_CREATE_SURFACE   0x0010u
#define LDP_CMD_DESTROY_SURFACE  0x0011u
#define LDP_CMD_ATTACH_BUFFER    0x0012u
#define LDP_CMD_SWAP_BUFFERS     0x0013u
#define LDP_CMD_DAMAGE_RECT      0x0014u
#define LDP_CMD_SET_TITLE        0x0015u
#define LDP_CMD_SET_STATE        0x0016u
#define LDP_CMD_SET_GLASS_EFFECT 0x0017u
#define LDP_CMD_ACK_EVENT        0x0018u

/* ── Server Event Opcodes (Compositor -> Client) ── */
#define LDP_EVT_SURFACE_CREATED  0x0100u
#define LDP_EVT_SURFACE_CONFIG   0x0101u /* Resize / configure geometry */
#define LDP_EVT_MOUSE_MOVE       0x0102u
#define LDP_EVT_MOUSE_BUTTON     0x0103u
#define LDP_EVT_KEY_PRESS        0x0104u
#define LDP_EVT_KEY_RELEASE      0x0105u
#define LDP_EVT_FOCUS_CHANGED    0x0106u
#define LDP_EVT_VSYNC_FRAME      0x0107u /* VSync tick for frame pacing */
#define LDP_EVT_CLOSE_REQUEST    0x0108u

/* ── Mouse Button Masks ── */
#define LDP_MOUSE_BTN_LEFT       0x01u
#define LDP_MOUSE_BTN_RIGHT      0x02u
#define LDP_MOUSE_BTN_MIDDLE     0x04u

/* ── Compact Fixed 32-Byte Packet Structure (Cache-Line Friendly) ── */
typedef struct {
    u16 opcode;         /* LDP_CMD_* or LDP_EVT_* */
    u16 surface_id;     /* Surface index */
    u32 client_id;      /* Client task/process ID */
    u32 param1;         /* X coord, width, or button mask */
    u32 param2;         /* Y coord, height, or keycode */
    u32 param3;         /* Flags, stride, or blur radius */
    u32 param4;         /* Format, state, or opacity */
    u64 timestamp;      /* High-precision hardware TSC timestamp */
} __attribute__((packed)) ldp_packet_t;

/* ── Damage Rectangle for Partial Dirty Recomposition ── */
typedef struct {
    u32 x;
    u32 y;
    u32 width;
    u32 height;
} ldp_rect_t;

/* ── Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer ── */
typedef struct {
    volatile u32 head;  /* Write index (modified by producer) */
    volatile u32 tail;  /* Read index (modified by consumer) */
    u32 capacity;       /* Number of packets (LDP_RING_BUFFER_SIZE) */
    u32 mask;           /* capacity - 1 */
    ldp_packet_t entries[LDP_RING_BUFFER_SIZE];
} ldp_ring_buffer_t;

/* ── Surface Cyber-Glass Attributes ── */
typedef struct {
    u8  enabled;        /* Glass effect enabled */
    u8  blur_radius;    /* Background blur radius (1..32 pixels) */
    u8  opacity;        /* Surface opacity (0..255) */
    u8  vibrancy;       /* Color vibrancy boost (0..100) */
    u32 tint_color;     /* Tint color (ARGB) */
    u32 border_color;   /* Specular border accent color */
} ldp_glass_attrs_t;

/* ── Shared Memory Surface Descriptor ── */
typedef struct {
    u32 surface_id;
    u32 client_id;
    u32 width;
    u32 height;
    u32 stride;
    u32 format;
    u32 flags;
    u8  state;
    char title[LDP_MAX_TITLE_LEN];

    /* Triple Buffering Geometry */
    u8  buffer_mode;
    u8  active_front;     /* Index of buffer currently displayed (0, 1, or 2) */
    u8  active_back;      /* Index of buffer ready for presentation */
    u8  active_render;    /* Index of buffer client is drawing into */
    u32 buffer_size;      /* Size of single frame in bytes */

    /* Pointers to buffers (in shared memory space) */
    u32* buffers[3];

    /* Cyber-Glass Visual Meta */
    ldp_glass_attrs_t glass;

    /* Dirty Damage Tracking */
    u8  damage_count;
    ldp_rect_t damages[LDP_MAX_DAMAGE_RECTS];

    /* Frame Statistics */
    u64 frame_sequence;
    u64 last_present_time;
} ldp_surface_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* LDP_PROTOCOL_H */
