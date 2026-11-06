#include "secure_io.h"
#include "keyboard.h"
#include "audit.h"
#include "io_guard.h"

void secure_io_init(void) {
}

int secure_io_keyboard_read_char(void) {
    int c = keyboard_read_char();
    int f = io_guard_filter_input(c);
    if (f < 0) {
        audit_log(AUDIT_EVT_TASK_VIOLATION, (u32)c, 0);
    }
    return f;
}

int secure_io_keyboard_try_read_char(void) {
    int c = keyboard_try_read_char();
    if (c < 0) return -1;
    c = io_guard_filter_input(c);
    if (c < 0) {
        audit_log(AUDIT_EVT_TASK_VIOLATION, 0, 1);
    }
    return c;
}
