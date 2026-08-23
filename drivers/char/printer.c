#include "printer.h"
#include "io.h"
#include "serial.h"
#include "string.h"
#include "spinlock.h"

static printer_status_t g_printer;
static spinlock_t g_printer_lock;

void printer_init(void) {
    spinlock_init(&g_printer_lock);
    memset(&g_printer, 0, sizeof(g_printer));
    str_lcpy(g_printer.name, "Generic IEEE-1284 Line Printer", sizeof(g_printer.name));

    /* Initialize LPT1 Controller: Send Reset Pulse (INIT low, then high) */
    outb(LPT1_BASE_PORT + LPT_REG_CONTROL, 0x00);
    io_wait();
    outb(LPT1_BASE_PORT + LPT_REG_CONTROL, LPT_CTRL_INIT | LPT_CTRL_SELECT);
    io_wait();

    /* Read status byte */
    u8 status = inb(LPT1_BASE_PORT + LPT_REG_STATUS);
    g_printer.present = 1u;
    g_printer.online = (status & LPT_STATUS_SELECT) ? 1u : 0u;
    g_printer.paper_out = (status & LPT_STATUS_PAPER_OUT) ? 1u : 0u;
    g_printer.busy = (status & LPT_STATUS_BUSY) ? 0u : 1u; /* Active low */
    g_printer.error = (status & LPT_STATUS_ERROR) ? 0u : 1u;

    serial_writeln("[PRINTER] Parallel Port / USB Line Printer Driver initialized (/dev/lp0)");
}

int printer_is_ready(void) {
    return g_printer.present ? 1 : 0;
}

int printer_write_char(u8 ch) {
    spinlock_lock(&g_printer_lock);

    /* Wait for printer to not be BUSY (timeout after 10000 polls) */
    for (u32 t = 0; t < 10000u; t++) {
        u8 st = inb(LPT1_BASE_PORT + LPT_REG_STATUS);
        if (st & LPT_STATUS_BUSY) {
            /* Ready to accept byte */
            break;
        }
        io_wait();
    }

    /* Output character data */
    outb(LPT1_BASE_PORT + LPT_REG_DATA, ch);
    io_wait();

    /* Assert STROBE pulse (active high in control register) */
    outb(LPT1_BASE_PORT + LPT_REG_CONTROL, LPT_CTRL_INIT | LPT_CTRL_SELECT | LPT_CTRL_STROBE);
    io_wait();
    /* De-assert STROBE */
    outb(LPT1_BASE_PORT + LPT_REG_CONTROL, LPT_CTRL_INIT | LPT_CTRL_SELECT);
    io_wait();

    g_printer.bytes_printed++;
    spinlock_unlock(&g_printer_lock);
    return 0;
}

int printer_write_buf(const u8* buf, u32 len) {
    if (!buf || len == 0) return -1;
    for (u32 i = 0; i < len; i++) {
        printer_write_char(buf[i]);
    }
    return 0;
}

int printer_print_text(const char* text) {
    if (!text) return -1;
    while (*text) {
        printer_write_char((u8)*text++);
    }
    /* Add form feed to eject page */
    printer_write_char('\n');
    printer_write_char('\f');
    return 0;
}

void printer_get_status(printer_status_t* out) {
    if (!out) return;
    spinlock_lock(&g_printer_lock);
    u8 status = inb(LPT1_BASE_PORT + LPT_REG_STATUS);
    g_printer.online = (status & LPT_STATUS_SELECT) ? 1u : 0u;
    g_printer.paper_out = (status & LPT_STATUS_PAPER_OUT) ? 1u : 0u;
    g_printer.busy = (status & LPT_STATUS_BUSY) ? 0u : 1u;
    g_printer.error = (status & LPT_STATUS_ERROR) ? 0u : 1u;
    memcpy(out, &g_printer, sizeof(printer_status_t));
    spinlock_unlock(&g_printer_lock);
}
