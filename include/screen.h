#ifndef SCREEN_H
#define SCREEN_H

#include "types.h"

/* Restore the VGA text hardware after the boot menu or any video-mode change. */
void screen_init(void);
void screen_dump_state(void);
void screen_clear(void);
void screen_putc(char c);
void screen_write(const char* s);
void screen_writeln(const char* s);
void screen_backspace(void);
void screen_set_color(u8 color);
u8 screen_get_color(void);
void screen_reset_color(void);
u32 screen_cols(void);
u32 screen_rows(void);

#endif
