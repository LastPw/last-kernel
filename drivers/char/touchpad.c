#include "touchpad.h"
#include "mouse.h"
#include "io.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

static touchpad_state_t g_pad;
static spinlock_t g_pad_lock;
static u8 g_packet[6];
static u8 g_packet_idx = 0;
static u32 g_last_x = 0;
static u32 g_last_y = 0;

void touchpad_init(void) {
    spinlock_init(&g_pad_lock);
    memset(&g_pad, 0, sizeof(g_pad));
    g_pad.present = 1u;
    g_pad.synaptics_mode = 1u;
    g_packet_idx = 0;
    serial_writeln("[TOUCHPAD] Synaptics Precision Touchpad Driver initialized (Multi-Touch/Gestures Active)");
}

int touchpad_is_available(void) {
    return g_pad.present ? 1 : 0;
}

void touchpad_feed_byte(u8 byte) {
    spinlock_lock(&g_pad_lock);

    /* Collect 6-byte Synaptics packet */
    g_packet[g_packet_idx++] = byte;
    if (g_packet_idx >= 6u) {
        g_packet_idx = 0u;

        /* Decode Synaptics 6-byte Absolute packet */
        u32 x = (u32)(((g_packet[3] & 0x10u) ? 0x1000 : 0) |
                     (((u32)g_packet[1] & 0x0Fu) << 8) |
                     (u32)g_packet[4]);

        u32 y = (u32)(((g_packet[3] & 0x20u) ? 0x1000 : 0) |
                     (((u32)g_packet[1] & 0xF0u) << 4) |
                     (u32)g_packet[5]);

        u32 z = (u32)g_packet[2]; /* Pressure */
        u8 w = (u8)((g_packet[0] >> 4) & 0x01) | ((g_packet[0] >> 1) & 0x02);

        g_pad.x = x;
        g_pad.y = y;
        g_pad.z_pressure = z;

        if (z > 30u) {
            /* Finger touching touchpad */
            g_pad.touching = 1u;

            if (w == 0u) {
                /* 2 fingers: Two-finger scroll */
                g_pad.finger_count = 2u;
                if (g_last_y != 0) {
                    i32 dy = (i32)y - (i32)g_last_y;
                    if (dy > 15 || dy < -15) {
                        /* Feed scroll delta */
                        mouse_feed_delta(0, dy / 10, 0);
                    }
                }
            } else {
                /* 1 finger: Move cursor */
                g_pad.finger_count = 1u;
                if (g_last_x != 0 && g_last_y != 0) {
                    i32 dx = ((i32)x - (i32)g_last_x) / 8;
                    i32 dy = -((i32)y - (i32)g_last_y) / 8;
                    if (dx >= -100 && dx <= 100 && dy >= -100 && dy <= 100) {
                        mouse_feed_delta(dx, dy, (g_packet[0] & 0x01) ? 1 : 0);
                    }
                }
            }

            g_last_x = x;
            g_last_y = y;
        } else {
            /* Finger lifted */
            g_pad.touching = 0u;
            g_pad.finger_count = 0u;
            g_last_x = 0;
            g_last_y = 0;
        }
    }

    spinlock_unlock(&g_pad_lock);
}

void touchpad_get_state(touchpad_state_t* out) {
    if (!out) return;
    spinlock_lock(&g_pad_lock);
    memcpy(out, &g_pad, sizeof(touchpad_state_t));
    spinlock_unlock(&g_pad_lock);
}
