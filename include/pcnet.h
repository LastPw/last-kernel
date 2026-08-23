#ifndef PCNET_H
#define PCNET_H

#include "types.h"

typedef int (*pcnet_rx_handler_t)(const u8* frame, u16 len);

int pcnet_init(void);
int pcnet_ready(void);
int pcnet_get_mac(u8* out_mac);
int pcnet_send(const u8* frame, u16 len);
u32 pcnet_poll(u32 budget, pcnet_rx_handler_t handler);

#endif
