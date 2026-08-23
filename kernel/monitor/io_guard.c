#include "io_guard.h"
#include "threat_logger.h"

void io_guard_init(void) {
}

void io_guard_on_irq(u8 irq, u32 irq_count) {
    if (irq > 15) {
        threat_logger_log(THREAT_IRQ_ABUSE, 0, irq, irq_count);
    }
}

int io_guard_filter_input(int ch) {
    if (ch == '\n' || ch == '\b' || ch == '\t' || ch == 3 || ch == 20 || ch == 22) {
        return ch;
    }

    if (ch == 0x81 || ch == 0x82 || ch == 0x83 || ch == 0x84) {
        return ch;
    }

    if (ch < 32 || ch > 126) {
        threat_logger_log(THREAT_IO_MALFORMED, 0, (u32)ch, 0);
        return -1;
    }

    return ch;
}
