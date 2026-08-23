#include "screen.h"
#include "io.h"
#include "serial.h"
#include "bootinfo.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_DEFAULT_COLOR 0x0F
#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA 0x3D5
#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_INDEX 0x3C4
#define VGA_SEQ_DATA 0x3C5
#define VGA_GC_INDEX 0x3CE
#define VGA_GC_DATA 0x3CF
#define VGA_AC_INDEX 0x3C0
#define VGA_AC_READ 0x3C1
#define VGA_INSTAT_READ 0x3DA

static u16* vga = (u16*)0xB8000;
static u32 row = 0;
static u32 col = 0;
static u8 current_color = VGA_DEFAULT_COLOR;

static void vga_write_text_mode_regs(const u8* regs) {
    outb(VGA_MISC_WRITE, *regs++);

    for (u8 i = 0; i < 5u; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, *regs++);
    }

    outb(VGA_CRTC_INDEX, 0x03);
    outb(VGA_CRTC_DATA, (u8)(inb(VGA_CRTC_DATA) | 0x80u));
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, (u8)(inb(VGA_CRTC_DATA) & 0x7Fu));
    for (u8 i = 0; i < 25u; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, *regs++);
    }

    for (u8 i = 0; i < 9u; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, *regs++);
    }

    for (u8 i = 0; i < 21u; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        outb(VGA_AC_INDEX, *regs++);
    }
    (void)inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
}

void screen_init(void) {
    if (g_boot_framebuffer_phys != 0) {
        /* In graphical framebuffer mode, do not reprogram legacy VGA registers */
        row = 0;
        col = 0;
        current_color = VGA_DEFAULT_COLOR;
        vga = (u16*)0xB8000;
        return;
    }
    /*
     * Restore every VGA register used by BIOS mode 03h.  Restoring only the
     * character map is insufficient after a graphics boot menu: VirtualBox
     * may retain the sequencer's screen-off bit and leave the CLI blank.
     */
    static const u8 mode_03h[] = {
        0x67,
        0x03, 0x00, 0x03, 0x00, 0x02,
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00,
        0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50, 0x9C, 0x0E,
        0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF,
        0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38,
        0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x0C, 0x00,
        0x0F, 0x08, 0x00
    };
    vga_write_text_mode_regs(mode_03h);
    row = 0;
    col = 0;
    current_color = VGA_DEFAULT_COLOR;
    /* early_mmio_map() (kernel/core.c) identity-maps 0xB8000 before this
       runs; Limine cleared the low map, so the raw address is only valid
       after that call. */
    vga = (u16*)0xB8000;
}

static void dump_hex8(u8 v) {
    static const char hexchars[] = "0123456789ABCDEF";
    serial_putc(hexchars[(v >> 4) & 0xFu]);
    serial_putc(hexchars[v & 0xFu]);
}

void screen_dump_state(void) {
    serial_write("[scrn-dump] misc=");
    dump_hex8((u8)inb(VGA_MISC_WRITE));
    serial_write(" sr:");
    for (u8 i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        dump_hex8((u8)inb(VGA_SEQ_DATA));
    }
    serial_write(" crtc:");
    for (u8 i = 0; i < 0x18; i++) {
        outb(VGA_CRTC_INDEX, i);
        dump_hex8((u8)inb(VGA_CRTC_DATA));
    }
    serial_write(" gc:");
    for (u8 i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        dump_hex8((u8)inb(VGA_GC_DATA));
    }
    serial_write(" ac:");
    for (u8 i = 0; i < 0x15; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_AC_INDEX, i);
        dump_hex8((u8)inb(VGA_AC_READ));
    }
    (void)inb(VGA_INSTAT_READ);
    outb(VGA_AC_INDEX, 0x20);
    /* BGA/VBE probe: is the VBE mode still enabled? */
    outw(0x1CE, 0x04u); serial_write(" bga_en="); dump_hex8((u8)inw(0x1CF));
    outw(0x1CE, 0x01u); serial_write(" xres="); dump_hex8((u8)inw(0x1CF));
    serial_write("\n");
}

static void cursor_enable(void) {
    if (g_boot_framebuffer_phys != 0) return;
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, 0x0E);
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, 0x0F);
}

static void cursor_update(void) {
    if (g_boot_framebuffer_phys != 0) return;
    u32 safe_row = row;
    u32 safe_col = col;
    if (safe_row >= VGA_HEIGHT) {
        safe_row = VGA_HEIGHT - 1;
    }
    if (safe_col >= VGA_WIDTH) {
        safe_col = VGA_WIDTH - 1;
    }

    u16 pos = (u16)(safe_row * VGA_WIDTH + safe_col);
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (u8)(pos & 0xFF));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (u8)((pos >> 8) & 0xFF));
}

static void scroll_if_needed(void) {
    if (row < VGA_HEIGHT) {
        return;
    }

    for (u32 y = 1; y < VGA_HEIGHT; y++) {
        for (u32 x = 0; x < VGA_WIDTH; x++) {
            vga[(y - 1) * VGA_WIDTH + x] = vga[y * VGA_WIDTH + x];
        }
    }

    for (u32 x = 0; x < VGA_WIDTH; x++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = ((u16)current_color << 8) | ' ';
    }

    row = VGA_HEIGHT - 1;
}

void screen_clear(void) {
    for (u32 y = 0; y < VGA_HEIGHT; y++) {
        for (u32 x = 0; x < VGA_WIDTH; x++) {
            vga[y * VGA_WIDTH + x] = ((u16)current_color << 8) | ' ';
        }
    }
    row = 0;
    col = 0;
    cursor_enable();
    cursor_update();
}

void screen_putc(char c) {
    if (c == '\n') {
        col = 0;
        row++;
        scroll_if_needed();
        cursor_update();
        return;
    }

    if (c == '\r') {
        col = 0;
        cursor_update();
        return;
    }

    if (col >= VGA_WIDTH) {
        col = 0;
        row++;
        scroll_if_needed();
    }

    vga[row * VGA_WIDTH + col] = ((u16)current_color << 8) | (u8)c;
    col++;
    if (col >= VGA_WIDTH) {
        col = 0;
        row++;
        scroll_if_needed();
    }
    cursor_update();
}

void screen_write(const char* s) {
    while (*s) {
        screen_putc(*s++);
    }
}

void screen_writeln(const char* s) {
    screen_write(s);
    screen_putc('\n');
}

void screen_backspace(void) {
    if (col == 0 && row == 0) {
        return;
    }

    if (col == 0) {
        row--;
        col = VGA_WIDTH - 1;
    } else {
        col--;
    }

    vga[row * VGA_WIDTH + col] = ((u16)current_color << 8) | ' ';
    cursor_update();
}

void screen_set_color(u8 color) {
    current_color = color;
}

u8 screen_get_color(void) {
    return current_color;
}

void screen_reset_color(void) {
    current_color = VGA_DEFAULT_COLOR;
}

u32 screen_cols(void) {
    return VGA_WIDTH;
}

u32 screen_rows(void) {
    return VGA_HEIGHT;
}
