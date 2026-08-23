#ifndef LASTSHARK_H
#define LASTSHARK_H

#include "types.h"

typedef enum {
    LSHARK_FILTER_ALL = 0,
    LSHARK_FILTER_TCP = 1,
    LSHARK_FILTER_UDP = 2,
    LSHARK_FILTER_ICMP = 3
} lastshark_filter_t;

void lastshark_init(void);
void lastshark_tick(void);
void lastshark_on_frame(const u8* frame, u16 len);
int lastshark_command(const char* args);

#endif
