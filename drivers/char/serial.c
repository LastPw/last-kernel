#include "serial.h"
#include "io.h"

#define COM1_PORT 0x3F8

static int serial_ready = 0;

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);

    serial_ready = 1;
}

static int tx_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

int serial_try_read_char(void) {
    if (!serial_ready || (inb(COM1_PORT + 5u) & 0x01u) == 0u) return -1;
    return (int)inb(COM1_PORT);
}

void serial_putc(char c) {
    if (!serial_ready) {
        return;
    }

    while (!tx_empty()) {
    }

    outb(COM1_PORT, (u8)c);
}

void serial_write(const char* s) {
    if (!serial_ready) {
        return;
    }

    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

void serial_writeln(const char* s) {
    serial_write(s);
    serial_write("\n");
}

void serial_write_dec(u32 value) {
    char digits[10];
    u32 count = 0u;
    if (value == 0u) {
        serial_putc('0');
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u) serial_putc(digits[--count]);
}

void serial_write_hex(u32 value) {
    char hexchars[] = "0123456789ABCDEF";
    u8 started = 0u;
    for (int i = 7; i >= 0; i--) {
        u8 nib = (u8)((value >> (i * 4)) & 0xFu);
        if (nib || started || i == 0) {
            started = 1u;
            serial_putc(hexchars[nib]);
        }
    }
}
