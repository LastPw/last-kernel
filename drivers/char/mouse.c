#include "mouse.h"
#include "io.h"
#include "gfx.h"

#define MOUSE_DATA_PORT 0x60
#define MOUSE_CMD_PORT  0x64
#define MOUSE_STATUS_OUTPUT_FULL 0x01u
#define MOUSE_STATUS_AUX_OUTPUT  0x20u

static mouse_state_t g_mouse;
static u8 pkt[4];
static u8 pkt_i = 0;
static u8 s_pkt_len = 3u;   /* 4 once an IntelliMouse wheel device answers */
static u32 g_mouse_max_x = 1023u;
static u32 g_mouse_max_y = 767u;

static int mouse_wait_write(void) {
    for (u32 i = 0; i < 100000u; i++) {
        if ((inb(MOUSE_CMD_PORT) & 0x02u) == 0u) return 0;
    }
    return -1;
}

static int mouse_wait_read(void) {
    for (u32 i = 0; i < 100000u; i++) {
        if ((inb(MOUSE_CMD_PORT) & 0x01u) != 0u) return 0;
    }
    return -1;
}

static int mouse_write(u8 v) {
    if (mouse_wait_write() != 0) return -1;
    outb(MOUSE_CMD_PORT, 0xD4);
    if (mouse_wait_write() != 0) return -1;
    outb(MOUSE_DATA_PORT, v);
    if (mouse_wait_read() != 0) return -1;
    (void)inb(MOUSE_DATA_PORT); /* ACK */
    return 0;
}

/* Read one byte straight from the aux device, used only for command replies
   that are longer than the single ACK mouse_write() consumes. */
static int mouse_read_byte(u8* out) {
    if (mouse_wait_read() != 0) return -1;
    *out = (u8)inb(MOUSE_DATA_PORT);
    return 0;
}

/* Three sample-rate changes in the order 200/100/80 is the standard trick that
   makes a PS/2 wheel mouse reveal itself: the device then reports ID 3 and
   switches to 4-byte packets whose fourth byte is the wheel.  QEMU's emulated
   PS/2 mouse answers it exactly as real hardware does.  A plain mouse replies
   ID 2 (or one byte) and stays on 3-byte packets. */
static u8 mouse_probe_wheel(void) {
    u8 ack = 0u, id = 0u;

    if (mouse_write(0xF3) != 0) return 0;
    if (mouse_write(200u) != 0) return 0;
    if (mouse_write(0xF3) != 0) return 0;
    if (mouse_write(100u) != 0) return 0;
    if (mouse_write(0xF3) != 0) return 0;
    if (mouse_write(80u) != 0) return 0;

    /* 0xF2 (Read ID) answers with 0x00 then the ID, so the reply has to be
       read by hand rather than through mouse_write()'s single-ACK consume. */
    if (mouse_wait_write() != 0) return 0;
    outb(MOUSE_CMD_PORT, 0xD4);
    if (mouse_wait_write() != 0) return 0;
    outb(MOUSE_DATA_PORT, 0xF2);
    if (mouse_read_byte(&ack) != 0) return 0;
    if (mouse_read_byte(&id) != 0) return 0;

    (void)mouse_write(0xF4);   /* re-enable streaming after the probe */
    return (id == 3u) ? 1u : 0u;
}

void mouse_init(void) {
    g_mouse.x = (i32)(g_mouse_max_x / 2u);
    g_mouse.y = (i32)(g_mouse_max_y / 2u);
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.buttons = 0;
    g_mouse.wheel = 0;
    g_mouse.seq = 0;
    pkt_i = 0;
    s_pkt_len = 3u;

    if (mouse_wait_write() == 0) {
        outb(MOUSE_CMD_PORT, 0xA8); /* enable aux device */
    }
    if (mouse_wait_write() == 0) {
        outb(MOUSE_CMD_PORT, 0x20);
    }
    if (mouse_wait_read() == 0) {
        u8 st = inb(MOUSE_DATA_PORT);
        st |= 0x01u; /* IRQ1 enable */
        st |= 0x02u; /* IRQ12 enable */
        st |= 0x40u; /* Scan-code translation */
        st &= (u8)~0x10u;
        if (mouse_wait_write() == 0) outb(MOUSE_CMD_PORT, 0x60);
        if (mouse_wait_write() == 0) outb(MOUSE_DATA_PORT, st);
    }

    (void)mouse_write(0xF6); /* defaults */
    (void)mouse_write(0xF4); /* enable streaming */

    /* After streaming is up, ask the device whether it is a wheel mouse.
       Getting this wrong costs nothing: an ID-2 device keeps sending 3-byte
       packets and s_pkt_len stays at 3. */
    if (mouse_probe_wheel()) s_pkt_len = 4u;
}

void mouse_set_bounds(u32 max_x, u32 max_y) {
    g_mouse_max_x = max_x > 0u ? max_x : 1023u;
    g_mouse_max_y = max_y > 0u ? max_y : 767u;
    if (g_mouse.x > (i32)g_mouse_max_x) g_mouse.x = (i32)g_mouse_max_x;
    if (g_mouse.y > (i32)g_mouse_max_y) g_mouse.y = (i32)g_mouse_max_y;
}

void mouse_set_pos(i32 x, i32 y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (i32)g_mouse_max_x) x = (i32)g_mouse_max_x;
    if (y > (i32)g_mouse_max_y) y = (i32)g_mouse_max_y;
    g_mouse.x = x;
    g_mouse.y = y;
}

void mouse_on_irq(void* regs) {
    u8 status;
    (void)regs;
    status = inb(MOUSE_CMD_PORT);
    if ((status & MOUSE_STATUS_OUTPUT_FULL) == 0u) return;
    if ((status & MOUSE_STATUS_AUX_OUTPUT) == 0u) return;

    u8 b = inb(MOUSE_DATA_PORT);
    if (pkt_i == 0u) {
        if ((b & 0x08u) == 0u) {
            /* Sync bit must be 1 on byte 0; if not, ignore and resync */
            return;
        }
    }
    pkt[pkt_i++] = b;
    if (pkt_i < s_pkt_len) return;
    pkt_i = 0;

    /* Overflow check (bits 6 and 7 of packet header) */
    if (pkt[0] & 0xC0u) return;

    i32 dx = (i32)pkt[1];
    if (pkt[0] & 0x10u) dx -= 256;

    i32 dy = (i32)pkt[2];
    if (pkt[0] & 0x20u) dy -= 256;

    g_mouse.dx = dx;
    g_mouse.dy = dy;

    /* Smooth 1:1 Precision Movement with gentle velocity curve */
    i32 adx = dx >= 0 ? dx : -dx;
    i32 ady = dy >= 0 ? dy : -dy;
    i32 mult_x = (adx >= 14) ? 2 : 1;
    i32 mult_y = (ady >= 14) ? 2 : 1;

    g_mouse.x += (dx * mult_x);
    g_mouse.y -= (dy * mult_y); /* In PS/2, positive dy is up, negative dy is down */

    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x > (i32)g_mouse_max_x) g_mouse.x = (i32)g_mouse_max_x;
    if (g_mouse.y > (i32)g_mouse_max_y) g_mouse.y = (i32)g_mouse_max_y;

    g_mouse.buttons = (u8)(pkt[0] & 0x07u);
    /* Fourth byte of an IntelliMouse packet: signed notch count, positive when
       the wheel rolls toward the user.  Bits 4-5 are the side buttons and are
       deliberately not folded into g_mouse.buttons, which is a 3-bit legacy
       mask that every existing reader already trusts. */
    g_mouse.wheel = (s_pkt_len == 4u) ? (i8)pkt[3] : 0;
    g_mouse.seq++;
}

void mouse_state(mouse_state_t* out) {
    if (!out) return;
    *out = g_mouse;
}

void mouse_feed_delta(i32 dx, i32 dy, u8 buttons) {
    if (dx == 0 && dy == 0) return;
    g_mouse.dx = dx;
    g_mouse.dy = dy;
    g_mouse.x += dx;
    g_mouse.y -= dy;
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x > (i32)g_mouse_max_x) g_mouse.x = (i32)g_mouse_max_x;
    if (g_mouse.y > (i32)g_mouse_max_y) g_mouse.y = (i32)g_mouse_max_y;
    g_mouse.buttons = buttons;
    g_mouse.seq++;
}

void mouse_draw_cursor(u32 x, u32 y) {
    gfx_draw_cursor(x, y);
}

void mouse_poll(void) {
    while ((inb(MOUSE_CMD_PORT) & (MOUSE_STATUS_OUTPUT_FULL | MOUSE_STATUS_AUX_OUTPUT)) ==
           (MOUSE_STATUS_OUTPUT_FULL | MOUSE_STATUS_AUX_OUTPUT)) {
        mouse_on_irq(0);
    }
}

