#include "bios_ui.h"

static inline UINT8 inb(UINT16 p)
{
    UINT8 v;
    __asm__ volatile ("inb %w1, %b0" : "=a"(v) : "Nd"(p));
    return v;
}

#define VGA_TEXT 0xB8000u
#define COLS 80

static VOID vga_put(INT32 row, INT32 col, INT32 attr, const char *s)
{
    /* logical -> VGA attribute byte (fg | bg << 4) */
    static const UINT8 amap[] = { 0x07, 0x0E, 0x07, 0x70, 0x0B };
    volatile UINT16 *v = (volatile UINT16 *)(VGA_TEXT) +
                         (UINTN)row * COLS + (UINTN)col;
    UINT8 a = amap[attr];
    while (*s)
        *v++ = (UINT16)((a << 8) | (UINT8)*s++);
}

/* PS/2 set-1 scan codes; E0-prefixed arrows handled via pending flag */
static UINT8 e0_pending;

static INT32 ps2_getkey(VOID)
{
    for (;;) {
        if (!(inb(0x64) & 1))
            return KEY_NONE;
        UINT8 sc = inb(0x60);
        if (sc == 0xE0) {
            e0_pending = 1;
            continue;
        }
        if (sc & 0x80) {                /* break code */
            e0_pending = 0;
            continue;
        }
        if (e0_pending) {
            e0_pending = 0;
            if (sc == 0x48) return KEY_UP;
            if (sc == 0x50) return KEY_DOWN;
            continue;
        }
        if (sc == 0x1C)                 /* Enter */
            return KEY_ENTER;
        if (sc == 0x11)                 /* w */
            return KEY_UP;
        if (sc == 0x1F)                 /* s */
            return KEY_DOWN;
    }
}

static VOID p_msleep(UINT32 ms)
{
    /* ~1 us per port-0x80 write; crude but fine for menu countdowns */
    for (UINT32 i = 0; i < ms * 1000u; i++)
        inb(0x80);
}

UINTN bios_menu(UINTN count, const char *const *titles,
                UINT32 timeout_s, UINTN preselect)
{
    menu_io_t io = { vga_put, ps2_getkey, p_msleep };
    menu_desc_t d;
    d.titles = titles;
    d.count = count;
    d.selected = preselect;
    d.timeout_s = timeout_s ? timeout_s : 5;
    d.header = "== Pulse Loader (BIOS) ==";
    return menu_run(&io, &d);
}
