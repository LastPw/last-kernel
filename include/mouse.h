#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

typedef struct {
    i32 x;
    i32 y;
    i32 dx;
    i32 dy;
    u8 buttons;
    u32 seq;
} mouse_state_t;

void mouse_init(void);
void mouse_on_irq(void* regs);
void mouse_state(mouse_state_t* out);
void mouse_set_bounds(u32 max_x, u32 max_y);
void mouse_set_pos(i32 x, i32 y);

/* Feed external (e.g. xHCI USB HID mouse) delta into the mouse state */
void mouse_feed_delta(i32 dx, i32 dy, u8 buttons);

/* Draw modern anti-aliased hardware cursor pointer at (x, y) */
void mouse_draw_cursor(u32 x, u32 y);

/* Poll for pending mouse packets from hardware */
void mouse_poll(void);

#endif /* MOUSE_H */
