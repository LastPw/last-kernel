/*
 * =============================================================================
 * Author: Taha Vaziry
 * File: ldp_tests.c
 * Purpose: Automated Unit Test Matrix for Last Display Protocol (LDP)
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "display/ldp_protocol.h"
#include "display/ldp_server.h"
#include "display/ldp_client.h"

/* ── Test 1: SPSC Lock-Free Ring Buffer ── */
static void test_ldp_ring_buffer(void) {
    printf("[LDP_TEST] 1. Testing Lock-Free SPSC Ring Buffer...\n");
    ldp_ring_buffer_t ring;
    ring.head = 0u;
    ring.tail = 0u;
    ring.capacity = LDP_RING_BUFFER_SIZE;
    ring.mask = LDP_RING_BUFFER_SIZE - 1u;

    assert(ldp_ring_count(&ring) == 0u);

    /* Push up to capacity */
    for (u32 i = 0; i < LDP_RING_BUFFER_SIZE; i++) {
        ldp_packet_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.opcode = LDP_CMD_DAMAGE_RECT;
        pkt.surface_id = (u16)i;
        pkt.param1 = i * 10u;
        int rc = ldp_ring_push(&ring, &pkt);
        assert(rc == 0);
    }

    assert(ldp_ring_count(&ring) == LDP_RING_BUFFER_SIZE);

    /* Push when full must fail */
    ldp_packet_t overflow_pkt;
    overflow_pkt.opcode = LDP_CMD_SWAP_BUFFERS;
    int rc_overflow = ldp_ring_push(&ring, &overflow_pkt);
    assert(rc_overflow == -1);

    /* Pop and verify FIFO order */
    for (u32 i = 0; i < LDP_RING_BUFFER_SIZE; i++) {
        ldp_packet_t out;
        int rc = ldp_ring_pop(&ring, &out);
        assert(rc == 0);
        assert(out.opcode == LDP_CMD_DAMAGE_RECT);
        assert(out.surface_id == (u16)i);
        assert(out.param1 == i * 10u);
    }

    assert(ldp_ring_count(&ring) == 0u);

    /* Pop when empty must fail */
    ldp_packet_t underflow_pkt;
    assert(ldp_ring_pop(&ring, &underflow_pkt) == -1);

    printf("  -> PASS: SPSC Ring buffer push/pop, saturation, and ordering verified!\n");
}

/* ── Test 2: Server Client Lifecycle ── */
static void test_ldp_server_lifecycle(void) {
    printf("[LDP_TEST] 2. Testing LDP Server Client Lifecycle...\n");
    ldp_server_init();
    ldp_server_t* srv = ldp_server_get_state();
    assert(srv->initialized == 1);
    assert(srv->running == 1);
    assert(srv->active_clients == 0);

    /* Connect 3 clients */
    int c0 = ldp_server_connect_client(1001);
    int c1 = ldp_server_connect_client(1002);
    int c2 = ldp_server_connect_client(1003);

    assert(c0 >= 0 && c1 >= 0 && c2 >= 0);
    assert(srv->active_clients == 3);
    assert(srv->clients[c0].client_pid == 1001);
    assert(srv->clients[c1].client_pid == 1002);
    assert(srv->clients[c2].client_pid == 1003);

    /* Disconnect middle client */
    ldp_server_disconnect_client((u32)c1);
    assert(srv->active_clients == 2);
    assert(srv->clients[c1].active == 0);

    /* Clean up remaining */
    ldp_server_disconnect_client((u32)c0);
    ldp_server_disconnect_client((u32)c2);
    assert(srv->active_clients == 0);

    printf("  -> PASS: Client connect, disconnect, and slot tracking verified!\n");
}

/* ── Test 3: Surface Creation & Glass Attributes ── */
static void test_ldp_surface_attributes(void) {
    printf("[LDP_TEST] 3. Testing Surface Creation & Cyber-Glass Meta...\n");
    ldp_server_init();
    ldp_server_t* srv = ldp_server_get_state();

    int cid = ldp_server_connect_client(2001);
    assert(cid >= 0);

    /* Create Cyber-Glass Surface */
    int sid = ldp_server_create_surface((u32)cid, "Terminal Glass Window", 1280, 720, LDP_FORMAT_ARGB8888, LDP_SURFACE_TOPLEVEL | LDP_SURFACE_GLASS_BLUR);
    assert(sid >= 0);

    ldp_surface_desc_t* desc = &srv->surfaces[sid];
    assert(desc->width == 1280);
    assert(desc->height == 720);
    assert(desc->stride == 1280 * 4);
    assert(desc->format == LDP_FORMAT_ARGB8888);
    assert(desc->glass.enabled == 1);
    assert(desc->glass.blur_radius == 16);
    assert(desc->glass.opacity == 220);
    assert(strcmp(desc->title, "Terminal Glass Window") == 0);

    /* Modify Cyber-Glass attributes */
    int ret_glass = ldp_server_set_glass_effect((u32)sid, 24, 180, 90, 0x10203040u, 0x50607080u);
    assert(ret_glass == 0);
    assert(desc->glass.blur_radius == 24);
    assert(desc->glass.opacity == 180);
    assert(desc->glass.vibrancy == 90);
    assert(desc->glass.tint_color == 0x10203040u);
    assert(desc->glass.border_color == 0x50607080u);

    ldp_server_destroy_surface((u32)sid);
    assert(srv->surface_active[sid] == 0);
    ldp_server_disconnect_client((u32)cid);

    printf("  -> PASS: Surface creation and Cyber-Glass metadata verified!\n");
}

/* ── Test 4: Zero-Copy Lock-Free Triple Buffering ── */
static void test_ldp_triple_buffering(void) {
    printf("[LDP_TEST] 4. Testing Zero-Copy Lock-Free Triple Buffering...\n");
    ldp_server_init();
    ldp_server_t* srv = ldp_server_get_state();

    int cid = ldp_server_connect_client(3001);
    int sid = ldp_server_create_surface((u32)cid, "Render Surface", 640, 480, LDP_FORMAT_ARGB8888, LDP_SURFACE_TOPLEVEL);
    assert(sid >= 0);

    /* Allocate dummy triple buffers (static to prevent stack overflow) */
    static u32 bufA[640 * 480];
    static u32 bufB[640 * 480];
    static u32 bufC[640 * 480];
    bufA[0] = 0xAA0000;
    bufB[0] = 0x00BB00;
    bufC[0] = 0x0000CC;

    int attach_rc = ldp_server_attach_buffers((u32)sid, bufA, bufB, bufC, 640 * 4);
    assert(attach_rc == 0);

    ldp_surface_desc_t* desc = &srv->surfaces[sid];
    assert(desc->active_front == 0);
    assert(desc->active_back == 1);
    assert(desc->active_render == 2);

    /* Client finishes drawing in active_render (slot 2) and requests swap */
    int swap_rc = ldp_server_swap_buffers((u32)sid);
    assert(swap_rc == 0);
    assert(desc->frame_sequence == 1);
    assert(desc->active_back == 2); /* Slot 2 is now back buffer */

    /* Compositor presents surface */
    int pres_rc = ldp_server_present_surface((u32)sid);
    assert(pres_rc == 0);
    assert(desc->active_front == 2); /* Slot 2 promoted to front */
    assert(ldp_server_get_front_buffer((u32)sid) == bufC);
    assert(srv->total_frames_composited == 1);

    ldp_server_destroy_surface((u32)sid);
    ldp_server_disconnect_client((u32)cid);

    printf("  -> PASS: Zero-Copy Triple Buffering swap and presentation verified!\n");
}

/* ── Test 5: Damage Tracking & Dirty Regions ── */
static void test_ldp_damage_tracking(void) {
    printf("[LDP_TEST] 5. Testing Damage Rectangle Tracking...\n");
    ldp_server_init();
    ldp_server_t* srv = ldp_server_get_state();

    int cid = ldp_server_connect_client(4001);
    int sid = ldp_server_create_surface((u32)cid, "Damage Test", 800, 600, LDP_FORMAT_ARGB8888, 0);
    assert(sid >= 0);

    ldp_surface_desc_t* desc = &srv->surfaces[sid];
    assert(desc->damage_count == 0);

    /* Add 3 damage rectangles */
    ldp_server_damage_rect((u32)sid, 10, 20, 100, 50);
    ldp_server_damage_rect((u32)sid, 200, 300, 50, 50);
    ldp_server_damage_rect((u32)sid, 400, 100, 80, 80);

    assert(desc->damage_count == 3);
    assert(desc->damages[0].x == 10 && desc->damages[0].width == 100);
    assert(desc->damages[1].x == 200 && desc->damages[1].height == 50);
    assert(desc->damages[2].x == 400 && desc->damages[2].width == 80);

    /* Presenting surface resets damage count */
    ldp_server_present_surface((u32)sid);
    assert(desc->damage_count == 0);

    ldp_server_destroy_surface((u32)sid);
    ldp_server_disconnect_client((u32)cid);

    printf("  -> PASS: Damage rectangle registration and reset verified!\n");
}

/* ── Test 6: Input Event Dispatching ── */
static void test_ldp_input_events(void) {
    printf("[LDP_TEST] 6. Testing Mouse & Keyboard Event Dispatching...\n");
    ldp_server_init();
    ldp_server_t* srv = ldp_server_get_state();

    int cid = ldp_server_connect_client(5001);
    int sid = ldp_server_create_surface((u32)cid, "Input Window", 800, 600, LDP_FORMAT_ARGB8888, 0);
    assert(sid >= 0);

    /* Dispatch mouse move */
    ldp_server_post_mouse_event((u32)sid, 340, 220, 0);

    /* Dispatch mouse button click */
    ldp_server_post_mouse_event((u32)sid, 340, 220, LDP_MOUSE_BTN_LEFT);

    /* Dispatch key press (Key 'A' = 0x41) */
    ldp_server_post_key_event((u32)sid, 0x41, 1);

    /* Dispatch key release */
    ldp_server_post_key_event((u32)sid, 0x41, 0);

    /* Read events from client event ring */
    ldp_ring_buffer_t* evt_ring = &srv->clients[cid].evt_ring;

    ldp_packet_t p0, p1, p2, p3, p4;
    /* First event was LDP_EVT_SURFACE_CREATED upon creation */
    assert(ldp_ring_pop(evt_ring, &p0) == 0);
    assert(p0.opcode == LDP_EVT_SURFACE_CREATED);

    /* Event 1: Mouse Move */
    assert(ldp_ring_pop(evt_ring, &p1) == 0);
    assert(p1.opcode == LDP_EVT_MOUSE_MOVE);
    assert((i32)p1.param1 == 340 && (i32)p1.param2 == 220);

    /* Event 2: Mouse Button Left */
    assert(ldp_ring_pop(evt_ring, &p2) == 0);
    assert(p2.opcode == LDP_EVT_MOUSE_BUTTON);
    assert(p2.param3 == LDP_MOUSE_BTN_LEFT);

    /* Event 3: Key Press */
    assert(ldp_ring_pop(evt_ring, &p3) == 0);
    assert(p3.opcode == LDP_EVT_KEY_PRESS);
    assert(p3.param1 == 0x41);

    /* Event 4: Key Release */
    assert(ldp_ring_pop(evt_ring, &p4) == 0);
    assert(p4.opcode == LDP_EVT_KEY_RELEASE);
    assert(p4.param1 == 0x41);

    ldp_server_destroy_surface((u32)sid);
    ldp_server_disconnect_client((u32)cid);

    printf("  -> PASS: Mouse and keyboard event dispatching verified!\n");
}

/* ── Test 7: Full Client SDK End-to-End Integration ── */
static u32 g_client_received_events = 0;
static void on_test_client_event(ldp_surface_t* surface, const ldp_packet_t* evt, void* user_data) {
    (void)surface;
    (void)user_data;
    if (evt->opcode == LDP_EVT_SURFACE_CREATED ||
        evt->opcode == LDP_EVT_MOUSE_MOVE ||
        evt->opcode == LDP_EVT_VSYNC_FRAME) {
        g_client_received_events++;
    }
}

static void test_ldp_client_sdk(void) {
    printf("[LDP_TEST] 7. Testing High-Level Client SDK Integration...\n");
    ldp_server_init();

    ldp_client_t* client = ldp_client_connect(6001);
    assert(client != NULL);
    assert(client->connected == 1);

    ldp_surface_t* surf = ldp_surface_create(client, "SDK Test Window", 640, 480, LDP_FORMAT_ARGB8888, LDP_SURFACE_TOPLEVEL | LDP_SURFACE_GLASS_BLUR);
    assert(surf != NULL);
    assert(surf->active == 1);

    /* Obtain drawing buffer and write pixels */
    u32* draw_buf = ldp_surface_get_draw_buffer(surf);
    assert(draw_buf != NULL);
    draw_buf[0] = 0xFFFFFFFFu;
    draw_buf[1] = 0x38BDF8u;

    /* Register damage and swap */
    ldp_surface_damage(surf, 0, 0, 2, 1);
    int swap_rc = ldp_surface_swap_buffers(surf);
    assert(swap_rc == 0);

    /* Set Cyber-Glass via SDK */
    int glass_rc = ldp_surface_set_glass(surf, 20, 200, 80, 0x11223344u);
    assert(glass_rc == 0);

    /* Post mouse move from compositor to test client polling */
    ldp_server_post_mouse_event(surf->surface_id, 100, 150, 0);

    /* Poll client events */
    g_client_received_events = 0;
    u32 polled = ldp_client_poll_events(client, on_test_client_event, NULL);
    assert(polled >= 2); /* Creation, VSync, Mouse Move */
    assert(g_client_received_events >= 2);

    /* Destroy and disconnect */
    ldp_surface_destroy(surf);
    ldp_client_disconnect(client);

    printf("  -> PASS: Complete Client SDK lifecycle and event polling verified!\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("====================================================================\n");
    printf("  Last Display Protocol (LDP) Architectural Test Suite             \n");
    printf("  Author: Taha Vaziry                                              \n");
    printf("====================================================================\n\n");

    test_ldp_ring_buffer();
    test_ldp_server_lifecycle();
    test_ldp_surface_attributes();
    test_ldp_triple_buffering();
    test_ldp_damage_tracking();
    test_ldp_input_events();
    test_ldp_client_sdk();

    printf("\n====================================================================\n");
    printf("  ALL 7 LDP PROTOCOL TESTS PASSED (100%% Vector Verification)       \n");
    printf("====================================================================\n");

    return 0;
}
