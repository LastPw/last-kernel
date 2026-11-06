/* ==============================================================================
 * LastKernel Enterprise Server Edition — Virtual Terminal (VTTY) Subsystem
 * Multi-Terminal Virtual Console Engine with Hotkey Multiplexing & Dedicated Shell
 * ============================================================================== */

#ifndef VTTY_H
#define VTTY_H

#include "types.h"

#define VTTY_MAX_TERMINALS      8
#define VTTY_ROWS               25
#define VTTY_COLS               80
#define VTTY_SCROLLBACK_LINES   500
#define VTTY_MAX_HISTORY        32
#define VTTY_CMD_BUF_SIZE       256

/* Hotkey Control Characters Emitted by Keyboard Driver */
#define HOTKEY_VTTY_BASE        0x90
#define HOTKEY_VTTY_1           0x91  /* Alt+F1 */
#define HOTKEY_VTTY_2           0x92  /* Alt+F2 */
#define HOTKEY_VTTY_3           0x93  /* Alt+F3 */
#define HOTKEY_VTTY_4           0x94  /* Alt+F4 */
#define HOTKEY_VTTY_5           0x95  /* Alt+F5 */
#define HOTKEY_VTTY_6           0x96  /* Alt+F6 */
#define HOTKEY_VTTY_7           0x97  /* Alt+F7 */
#define HOTKEY_VTTY_8           0x98  /* Alt+F8 */
#define HOTKEY_VTTY_PREV        0x88  /* Alt+Left */
#define HOTKEY_VTTY_NEXT        0x89  /* Alt+Right */

/* ── Virtual Terminal Data Structure ── */
typedef struct {
    u32 id;                            /* 0 to 7 (TTY 1 to 8) */
    char title[32];                    /* "sh", "lasttop", "storage", etc. */
    u16 screen_buffer[VTTY_ROWS * VTTY_COLS];
    u32 cursor_x;
    u32 cursor_y;
    u8  fg_color;
    u8  bg_color;
    
    char line_buf[VTTY_CMD_BUF_SIZE];
    u32 line_len;
    
    char history[VTTY_MAX_HISTORY][VTTY_CMD_BUF_SIZE];
    u32 history_count;
    int history_index;
    
    u8 is_active;
    u8 is_running_interactive_app;
} vtty_t;

/* ── Virtual Terminal Subsystem APIs ── */
void vtty_init(void);
void vtty_switch_to(u32 vtty_id);
void vtty_switch_next(void);
void vtty_switch_prev(void);
u32  vtty_get_active_id(void);
vtty_t* vtty_get(u32 vtty_id);
vtty_t* vtty_get_active(void);

void vtty_putc(vtty_t* vt, char c);
void vtty_puts(vtty_t* vt, const char* str);
void vtty_clear(vtty_t* vt);
void vtty_render_status_bar(void);
void vtty_flush_to_screen(vtty_t* vt);

int  vtty_handle_key_input(int key_char);
void vtty_execute_command(vtty_t* vt, const char* cmdline);

/* Dedicated Shell Commands */
void vtty_cmd_lastfetch(vtty_t* vt);
void vtty_cmd_lasttop(vtty_t* vt);
void vtty_cmd_lastctl(vtty_t* vt, const char* args);
void vtty_cmd_lastpkg(vtty_t* vt, const char* args);
void vtty_cmd_last_storage(vtty_t* vt, const char* args);
void vtty_cmd_last_net(vtty_t* vt, const char* args);
void vtty_cmd_last_sec(vtty_t* vt);
void vtty_cmd_last_vm(vtty_t* vt, const char* args);
void vtty_cmd_last_bench(vtty_t* vt);

#endif /* VTTY_H */
