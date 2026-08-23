#ifndef ETHERNET_H
#define ETHERNET_H

#include "types.h"

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV6 0x86DD
#define ETH_ADDR_LEN 6

typedef struct __attribute__((packed)) {
    u8 dst[ETH_ADDR_LEN];
    u8 src[ETH_ADDR_LEN];
    u16 ethertype;
} eth_header_t;

void ethernet_default_mac(u8* out_src, u8* out_dst);
void ethernet_set_local_mac(const u8* mac);
void ethernet_set_peer_mac(const u8* mac);
int ethernet_send_frame(const u8* frame, u16 len);

#endif
