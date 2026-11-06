#ifndef NIC_H
#define NIC_H

#include "types.h"

void nic_init(void);
int nic_ready(void);
const char* nic_backend_name(void);
int nic_get_mac(u8* out_mac);
int nic_send_frame(const u8* frame, u16 len);
u32 nic_poll(u32 budget);

#endif
