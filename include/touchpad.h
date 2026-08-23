#ifndef TOUCHPAD_H
#define TOUCHPAD_H

#include "types.h"

typedef struct {
    u8  present;
    u8  synaptics_mode;
    u8  finger_count;
    u8  touching;
    u8  tap_detected;
    u32 x;
    u32 y;
    u32 z_pressure;
    i32 scroll_dx;
    i32 scroll_dy;
} touchpad_state_t;

void touchpad_init(void);
int  touchpad_is_available(void);
void touchpad_feed_byte(u8 byte);
void touchpad_get_state(touchpad_state_t* out);

#endif /* TOUCHPAD_H */
