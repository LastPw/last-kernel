#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "types.h"

typedef int (*virtio_net_rx_handler_t)(const u8* frame, u16 len);

int virtio_net_init(void);
int virtio_net_ready(void);
int virtio_net_get_mac(u8* out_mac);
int virtio_net_send(const u8* frame, u16 len);
u32 virtio_net_poll(u32 budget, virtio_net_rx_handler_t handler);

#endif
