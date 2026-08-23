#ifndef PRINTER_H
#define PRINTER_H

#include "types.h"

#define LPT1_BASE_PORT       0x378u
#define LPT2_BASE_PORT       0x278u

#define LPT_REG_DATA         0x00u
#define LPT_REG_STATUS       0x01u
#define LPT_REG_CONTROL      0x02u

#define LPT_STATUS_ERROR     0x08u
#define LPT_STATUS_SELECT    0x10u
#define LPT_STATUS_PAPER_OUT 0x20u
#define LPT_STATUS_ACK       0x40u
#define LPT_STATUS_BUSY      0x80u

#define LPT_CTRL_STROBE      0x01u
#define LPT_CTRL_AUTOLF      0x02u
#define LPT_CTRL_INIT        0x04u
#define LPT_CTRL_SELECT      0x08u
#define LPT_CTRL_IRQ_ENABLE  0x10u

typedef struct {
    u8  present;
    u8  online;
    u8  paper_out;
    u8  busy;
    u8  error;
    u32 bytes_printed;
    char name[32];
} printer_status_t;

void printer_init(void);
int  printer_is_ready(void);
int  printer_write_char(u8 ch);
int  printer_write_buf(const u8* buf, u32 len);
int  printer_print_text(const char* text);
void printer_get_status(printer_status_t* out);

#endif /* PRINTER_H */
